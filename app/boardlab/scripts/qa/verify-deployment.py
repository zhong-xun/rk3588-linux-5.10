#!/usr/bin/env python3
"""Exercise synchronization against temporary SDK and rootfs projects."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DeploymentContract(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="boardlab-sync-qa-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.source = self.base / "source"
        self.projects = self.base / "projects"
        self.projects.mkdir()
        files = ["CMakeLists.txt", "VERSION", "README.md", "src/boardbackend.cpp",
                 "scripts/build.sh", "web/index.html"]
        for name in files:
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture\n")
        for name in ["assets", "third_party"]:
            (self.source / name).mkdir()
        helper = self.source / "scripts/sync-buildroot-boardlab-runtime.sh"
        helper.write_text("#!/bin/sh\nexit 88\n")
        helper.chmod(0o755)
        self.environment = dict(os.environ, BOARDLAB_SOURCE_ROOT=str(self.source),
                                TOPEET_ROOTFS_PROJECTS_ROOT=str(self.projects),
                                TOPEET_LINUX_ROOT=str(self.base / "absent-linux"))

    def run_sync(self, *arguments, expected=0):
        result = subprocess.run(
            [str(ROOT / "scripts/sync-sdk-boardlab.sh"), "--rootfs-only", *arguments],
            env=self.environment, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def test_dry_run_apply_check_and_idempotence(self):
        target = self.projects / "boardlab"
        self.run_sync("--dry-run")
        self.assertFalse(target.exists())
        self.run_sync("--check", expected=1)
        self.run_sync("--apply")
        self.run_sync("--check")
        before = target.stat().st_ino
        self.run_sync("--apply")
        self.assertEqual(target.stat().st_ino, before)
        self.assertFalse(list(self.projects.glob(".boardlab.previous.*")))

    def test_mode_drift_and_previous_source_preserved(self):
        self.run_sync("--apply")
        target = self.projects / "boardlab"
        (target / "scripts/build.sh").chmod(0o700)
        (target / "customer-notes.txt").write_text("keep customer changes\n")
        self.run_sync("--check", expected=1)
        self.run_sync("--apply")
        backups = list(self.projects.glob(".boardlab.previous.*"))
        self.assertEqual(len(backups), 1)
        self.assertEqual((backups[0] / "customer-notes.txt").read_text(),
                         "keep customer changes\n")
        self.run_sync("--check")

    def test_unrelated_symlink_is_rejected(self):
        unrelated = self.base / "unrelated"
        unrelated.mkdir()
        (self.projects / "boardlab").symlink_to(unrelated, target_is_directory=True)
        self.run_sync("--apply", expected=1)
        self.assertTrue((self.projects / "boardlab").is_symlink())
        self.assertEqual(list(unrelated.iterdir()), [])

    def test_runtime_package_backup_and_repeat_apply(self):
        linux = self.base / "linux"
        sdk = linux / "board/sdk"
        package = sdk / "buildroot/package/rockchip/boardlab"
        package.mkdir(parents=True)
        (package.parent / "Config.in").write_text("fixture\n")
        (package / "boardlab.mk").write_text("customer package\n")
        environment = dict(os.environ, TOPEET_LINUX_ROOT=str(linux))
        script = ROOT / "scripts/sync-buildroot-boardlab-runtime.sh"
        for mode in ["--apply", "--check", "--apply"]:
            result = subprocess.run([str(script), mode], env=environment,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        backups = list(sdk.glob(".boardlab-runtime.previous.*"))
        self.assertEqual(len(backups), 1)
        self.assertEqual((backups[0] / "package/rockchip/boardlab/boardlab.mk").read_text(),
                         "customer package\n")
        self.assertEqual((package / "S60boardlab").stat().st_mode & 0o777, 0o755)
        self.assertEqual((package / "Config.in").stat().st_mode & 0o777, 0o644)

    def test_init_reports_stopped_and_failed_start(self):
        original = ROOT / "packaging/buildroot/package/rockchip/boardlab/S60boardlab"
        script = self.base / "S60boardlab"
        contents = original.read_text()
        for path in ["/run/boardlab", "/var/log/boardlab", "/var/lib/boardlab",
                     "/var/cache/boardlab", "/usr/sbin/boardlab-supervisor",
                     "/usr/bin/boardlab"]:
            contents = contents.replace(path, str(self.base) + path)
        script.write_text(contents)
        for action, expected in [("status", 3), ("start", 1)]:
            result = subprocess.run(["sh", str(script), action], capture_output=True,
                                    text=True, timeout=15)
            self.assertEqual(result.returncode, expected, result.stdout + result.stderr)

    def test_supervisor_distinguishes_restart_from_crash(self):
        original = ROOT / "packaging/buildroot/package/rockchip/boardlab/boardlab-supervisor"
        for scenario, expected_code, expected_runs in [("normal", 0, 7), ("crash", 1, 5)]:
            with self.subTest(scenario=scenario):
                sandbox = self.base / scenario
                helpers = sandbox / "helpers"
                helpers.mkdir(parents=True)
                for name in ["sleep", "wayland-info", "pidof"]:
                    helper = helpers / name
                    helper.write_text("#!/bin/sh\nexit 0\n")
                    helper.chmod(0o755)
                app = helpers / "application"
                app.write_text("""#!/bin/sh
count=$(cat "$BOARDLAB_QA_COUNT" 2>/dev/null || printf 0)
count=$((count + 1))
printf '%s\\n' "$count" > "$BOARDLAB_QA_COUNT"
if [ "$BOARDLAB_QA_SCENARIO" = normal ]; then
    [ "$count" -le 6 ] && exit 0
    exit 20
fi
exit 1
""")
                app.chmod(0o755)
                runtime = sandbox / "run"
                runtime.mkdir()
                (runtime / "wayland-0").touch()
                contents = original.read_text()
                for prefix in ["/etc/", "/var/", "/run/", "/proc/"]:
                    contents = contents.replace(prefix, str(sandbox) + prefix)
                contents = contents.replace("/usr/bin/boardlab", str(app))
                supervisor = sandbox / "supervisor"
                supervisor.write_text(contents)
                count_file = sandbox / "count"
                environment = dict(os.environ, PATH=str(helpers) + ":" + os.environ["PATH"],
                                    XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY="wayland-0",
                                    BOARDLAB_WEB_TOKEN="qa", BOARDLAB_QA_COUNT=str(count_file),
                                    BOARDLAB_QA_SCENARIO=scenario)
                result = subprocess.run(["sh", str(supervisor)], env=environment,
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, expected_code, result.stdout + result.stderr)
                self.assertEqual(int(count_file.read_text()), expected_runs)
                self.assertEqual((sandbox / "var/lib/boardlab/safe-mode").exists(), scenario == "crash")


if __name__ == "__main__":
    unittest.main()
