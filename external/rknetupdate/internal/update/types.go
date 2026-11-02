package update

import "time"

type Target string

const (
	TargetUboot  Target = "uboot"
	TargetBoot   Target = "boot"
	TargetRootfs Target = "rootfs"
	TargetBundle Target = "bundle"
	// TargetUpdateImage is a complete Rockchip RKFW/RKAF package.  It is
	// deliberately not an ImageTarget: update.img must never be raw-written
	// to one of the individual partitions.
	TargetUpdateImage Target = "updateimg"
)

func (t Target) Valid() bool {
	return t == TargetUboot || t == TargetBoot || t == TargetRootfs || t == TargetBundle || t == TargetUpdateImage
}

func (t Target) ImageTarget() bool { return t == TargetUboot || t == TargetBoot || t == TargetRootfs }

func (t Target) Filename() string {
	if t == TargetBundle {
		return "bundle.manifest.json"
	}
	if t == TargetUpdateImage {
		return "update.img"
	}
	return string(t) + ".img"
}

type JobStatus string

const (
	StatusUploading             JobStatus = "uploading"
	StatusAwaiting              JobStatus = "awaiting_confirmation"
	StatusFlashing              JobStatus = "flashing"
	StatusVerifying             JobStatus = "verifying"
	StatusPreparingRecovery     JobStatus = "preparing_recovery"
	StatusRecoveryPrepared      JobStatus = "recovery_prepared"
	StatusRebootRequested       JobStatus = "reboot_requested"
	StatusSystemRebootRequested JobStatus = "system_reboot_requested"
	StatusSystemRebootFailed    JobStatus = "system_reboot_failed"
	StatusDryRunDone            JobStatus = "dry_run_complete"
	StatusSucceeded             JobStatus = "succeeded"
	StatusFailed                JobStatus = "failed"
	StatusCancelled             JobStatus = "cancelled"
)

type Validation struct {
	DetectedType string   `json:"detected_type"`
	Confidence   string   `json:"confidence"`
	Summary      string   `json:"summary"`
	Items        []string `json:"items,omitempty"`
}

// Artifact is one independently verified member of an offline bundle.  It is
// intentionally returned with the job so an operator can inspect every
// target/hash before confirming the recovery handoff.
type Artifact struct {
	Target         Target     `json:"target"`
	Filename       string     `json:"filename"`
	Size           int64      `json:"size"`
	SHA256         string     `json:"sha256"`
	Device         string     `json:"device"`
	ResolvedPath   string     `json:"resolved_device"`
	DeviceSize     int64      `json:"device_size"`
	Filesystem     string     `json:"filesystem,omitempty"`
	FilesystemUUID string     `json:"filesystem_uuid,omitempty"`
	BlockSize      uint64     `json:"block_size,omitempty"`
	BlockCount     uint64     `json:"block_count,omitempty"`
	Validation     Validation `json:"validation"`
	ReadbackSHA    string     `json:"readback_sha256,omitempty"`
}

type Job struct {
	ID               string     `json:"id"`
	Target           Target     `json:"target"`
	Filename         string     `json:"filename"`
	Size             int64      `json:"size"`
	SHA256           string     `json:"sha256"`
	Device           string     `json:"device"`
	ResolvedDevice   string     `json:"resolved_device"`
	ParentDevice     string     `json:"parent_device"`
	DeviceSize       int64      `json:"device_size,omitempty"`
	Status           JobStatus  `json:"status"`
	Progress         int        `json:"progress"`
	Validation       Validation `json:"validation"`
	Confirmation     string     `json:"confirmation_token,omitempty"`
	ReadbackSHA256   string     `json:"readback_sha256,omitempty"`
	Artifacts        []Artifact `json:"artifacts,omitempty"`
	RecoveryPhase    string     `json:"recovery_phase,omitempty"`
	Error            string     `json:"error,omitempty"`
	DryRun           bool       `json:"dry_run"`
	CreatedAt        time.Time  `json:"created_at"`
	UpdatedAt        time.Time  `json:"updated_at"`
	confirmationEnds time.Time

	imagePath    string
	manifestPath string
}

type StatusResponse struct {
	Busy bool `json:"busy"`
	Job  *Job `json:"job,omitempty"`
}

type Capability struct {
	Target         Target `json:"target"`
	Filename       string `json:"filename"`
	Device         string `json:"device"`
	ResolvedDevice string `json:"resolved_device,omitempty"`
	ParentDevice   string `json:"parent_device,omitempty"`
	Available      bool   `json:"available"`
	DeviceSize     int64  `json:"device_size,omitempty"`
	MaxUploadBytes int64  `json:"max_upload_bytes,omitempty"`
	Workflow       string `json:"workflow"`
	WriteEnabled   bool   `json:"write_enabled"`
	Error          string `json:"error,omitempty"`
}
