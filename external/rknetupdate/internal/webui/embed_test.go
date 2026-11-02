package webui

import (
	"io/fs"
	"strings"
	"testing"
)

func TestEmbeddedUpdateConsoleStructure(t *testing.T) {
	t.Parallel()

	checks := map[string][]string{
		"static/index.html": {
			`id="authGate"`,
			`id="authForm"`,
			`id="tokenInput"`,
			`id="changeTokenButton"`,
			`id="rebootDialog"`,
			`id="rebootNowButton"`,
			`id="rebootLaterButton"`,
			`aria-labelledby="rebootDialogTitle"`,
			`id="workflowBrief"`,
			`id="missionBanner"`,
			`id="stageBlock"`,
			`id="stageRail"`,
			`id="serialNotice"`,
			`id="powerWarning"`,
			`id="payloadList"`,
			`aria-live="polite"`,
		},
		"static/app.js": {
			`token: ""`,
			"AuthenticationError",
			"requireAuthentication",
			"async function authenticate",
			"directRebootVerified",
			"system_reboot_failed",
			"rebootPromptedJobs",
			`api("/api/v1/reboot"`,
			"job.readback_sha256",
			"updateimgStages",
			"renderStageRail",
			"离线阶段不在 Web 显示",
			"Web 跟踪已停止",
			"设备已重新上线，任务面板已重置",
			"validation?.items",
		},
		"static/style.css": {
			".auth-gate",
			".auth-card",
			".token-field",
			".reboot-dialog",
			".reboot-card",
			".stage-rail",
			".serial-notice",
			".mission-banner",
			".power-warning",
			":focus-visible",
		},
	}

	for name, required := range checks {
		data, err := fs.ReadFile(Files, name)
		if err != nil {
			t.Fatalf("read embedded %s: %v", name, err)
		}
		content := string(data)
		for _, fragment := range required {
			if !strings.Contains(content, fragment) {
				t.Errorf("%s is missing %q", name, fragment)
			}
		}
	}
}

func TestEmbeddedConsoleDoesNotShipFactoryToken(t *testing.T) {
	t.Parallel()

	data, err := fs.ReadFile(Files, "static/app.js")
	if err != nil {
		t.Fatalf("read embedded app.js: %v", err)
	}
	content := string(data)
	for _, forbidden := range []string{
		`token: "topeet"`,
		`localStorage.setItem("token"`,
		`sessionStorage.setItem(`,
	} {
		if strings.Contains(content, forbidden) {
			t.Errorf("static/app.js must not embed or persist the access token: found %q", forbidden)
		}
	}
}
