package update

import (
	"context"
	"crypto/subtle"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"strings"
	"syscall"
	"time"

	"rknetupdate/internal/smokecontrol"
)

const systemRebootDelay = 750 * time.Millisecond

type rebootWorkflow interface {
	Reboot(context.Context) error
}

type systemRebootWorkflow struct{}

func (systemRebootWorkflow) Reboot(ctx context.Context) error {
	_, err := smokecontrol.RequestCleanReboot(ctx, smokecontrol.RebootOperations{
		Stat: os.Stat,
		Sync: syscall.Sync,
		Run: func(ctx context.Context, path string, args ...string) error {
			output, err := exec.CommandContext(ctx, path, args...).CombinedOutput()
			if err == nil {
				return nil
			}
			if detail := strings.TrimSpace(string(output)); detail != "" {
				return fmt.Errorf("%w: %s", err, detail)
			}
			return err
		},
	})
	return err
}

// RequestSystemReboot accepts only a completed, readback-verified direct
// U-Boot/Boot job. The delayed worker gives the HTTP handler enough time to
// return 202 Accepted before the init system tears down the network service.
func (m *Manager) RequestSystemReboot(jobID string) (*Job, error) {
	m.mu.Lock()
	if m.busy {
		m.mu.Unlock()
		return nil, ErrBusy
	}
	if m.job == nil || m.job.ID != jobID {
		m.mu.Unlock()
		return nil, errors.New("completed update job not found")
	}
	job := m.job
	if job.Target != TargetUboot && job.Target != TargetBoot {
		m.mu.Unlock()
		return nil, errors.New("system reboot confirmation is only available after uboot or boot update")
	}
	if job.DryRun || (job.Status != StatusSucceeded && job.Status != StatusSystemRebootFailed) {
		m.mu.Unlock()
		return nil, fmt.Errorf("job is not a completed live update (status %s)", job.Status)
	}
	if job.SHA256 == "" || job.ReadbackSHA256 == "" || subtle.ConstantTimeCompare([]byte(job.SHA256), []byte(job.ReadbackSHA256)) != 1 {
		m.mu.Unlock()
		return nil, errors.New("completed job does not have a matching readback SHA-256")
	}
	if m.reboot == nil {
		m.mu.Unlock()
		return nil, errors.New("system reboot workflow is unavailable")
	}

	m.busy = true
	job.Status = StatusSystemRebootRequested
	job.Error = ""
	job.UpdatedAt = time.Now().UTC()
	response := cloneJob(job, true)
	m.wg.Add(1)
	m.mu.Unlock()

	go m.requestSystemReboot(jobID)
	m.logger.Info("system reboot requested after direct update", "job_id", jobID, "target", job.Target, "sha256", job.SHA256)
	return response, nil
}

func (m *Manager) requestSystemReboot(jobID string) {
	defer m.wg.Done()
	timer := time.NewTimer(systemRebootDelay)
	defer timer.Stop()
	<-timer.C

	m.mu.Lock()
	if m.job == nil || m.job.ID != jobID || m.job.Status != StatusSystemRebootRequested {
		m.mu.Unlock()
		return
	}
	reboot := m.reboot
	m.mu.Unlock()

	if err := reboot.Reboot(context.Background()); err != nil {
		m.mu.Lock()
		defer m.mu.Unlock()
		if m.job == nil || m.job.ID != jobID {
			return
		}
		// The partition write and readback verification are still valid. Keep a
		// distinct, retryable state instead of turning a successful flash into
		// an unrecoverable failed update job.
		m.job.Status = StatusSystemRebootFailed
		m.job.Error = fmt.Sprintf("request clean system reboot: %v", err)
		m.job.UpdatedAt = time.Now().UTC()
		m.busy = false
		m.logger.Error("system reboot request failed", "job_id", jobID, "error", err)
		return
	}
	m.logger.Info("clean system reboot command accepted", "job_id", jobID)
}
