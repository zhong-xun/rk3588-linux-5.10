#!/usr/bin/env python3
"""Host-only checks; no hardware operations or system installation."""

import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
BINARY = Path(os.environ.get("BOARDLAB_QA_BINARY", ROOT / "build/boardlab"))


class RuntimeContract(unittest.TestCase):
    def setUp(self):
        self.runtime = tempfile.TemporaryDirectory(prefix="boardlab-runtime-qa-")
        self.addCleanup(self.runtime.cleanup)
        self.environment = dict(os.environ, QT_QPA_PLATFORM="offscreen",
                                QT_QUICK_BACKEND="software",
                                XDG_RUNTIME_DIR=self.runtime.name,
                                BOARDLAB_WEB_TOKEN="qa-local-token")
        self.process = None

    def tearDown(self):
        if self.process is not None and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.communicate()

    def run_cli(self, *arguments):
        return subprocess.run([str(BINARY), *arguments], env=self.environment,
                              capture_output=True, text=True, timeout=15)

    def launch(self, *arguments):
        self.process = subprocess.Popen(
            [str(BINARY), "--framed", *arguments], env=self.environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def free_port(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            return listener.getsockname()[1]

    def test_invalid_arguments_fail_before_launch(self):
        for arguments in [("--web-port", "70000"), ("--web-port", "abc"),
                          ("--web-bind", "invalid"), ("--window-size", "0x480"),
                          ("--window-size", "800x480x1"),
                          ("--screenshot-delay", "-1"),
                          ("--virtual-keyboard", "invalid")]:
            with self.subTest(arguments=arguments):
                result = self.run_cli(*arguments)
                self.assertEqual(result.returncode, 2, result.stderr)

    def test_headless_profile(self):
        result = self.run_cli("--profile-json")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIsInstance(json.loads(result.stdout), dict)

    def test_web_opt_in_and_graceful_shutdown(self):
        port = self.free_port()
        self.launch("--web", "--web-bind", "127.0.0.1", "--web-port", str(port))
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            self.assertIsNone(self.process.poll())
            try:
                with urllib.request.urlopen("http://127.0.0.1:%d/healthz" % port,
                                            timeout=0.3) as response:
                    self.assertEqual(response.status, 200)
                break
            except OSError:
                time.sleep(0.1)
        else:
            self.fail("Web endpoint did not start")
        self.process.terminate()
        _, error = self.process.communicate(timeout=10)
        self.assertEqual(self.process.returncode, 0, error)

    def test_web_disabled_without_opt_in_and_override(self):
        for flags in [[], ["--web", "--no-web"]]:
            port = self.free_port()
            self.launch("--web-port", str(port), *flags)
            time.sleep(1)
            self.assertIsNone(self.process.poll())
            with socket.socket() as client:
                self.assertNotEqual(client.connect_ex(("127.0.0.1", port)), 0)
            self.process.terminate()
            _, error = self.process.communicate(timeout=10)
            self.assertEqual(self.process.returncode, 0, error)

    def wait_for_instance(self):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            self.assertIsNone(self.process.poll())
            if list(Path(self.runtime.name).glob("boardlab-*.lock")):
                return
            time.sleep(0.05)
        self.fail("Session lock did not appear")

    def test_duplicate_launch_activates_existing_instance(self):
        self.launch("--no-web")
        self.wait_for_instance()
        result = self.run_cli("--framed", "--no-web")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIsNone(self.process.poll())

    def test_supervisor_duplicate_reports_existing_owner(self):
        self.launch("--no-web")
        self.wait_for_instance()
        self.environment["BOARDLAB_SUPERVISED"] = "1"
        result = self.run_cli("--framed", "--no-web")
        self.assertEqual(result.returncode, 20, result.stderr)
        self.assertIsNone(self.process.poll())

    def test_lock_recovers_after_crash(self):
        self.launch("--no-web")
        self.wait_for_instance()
        self.process.kill()
        self.process.communicate(timeout=5)
        self.launch("--no-web")
        time.sleep(0.6)
        self.assertIsNone(self.process.poll())
        result = self.run_cli("--framed", "--no-web")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_diagnostic_instance_remains_independent(self):
        self.launch("--no-web")
        self.wait_for_instance()
        other = subprocess.Popen([str(BINARY), "--framed", "--no-web", "--allow-multiple"],
                                 env=self.environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            time.sleep(0.6)
            self.assertIsNone(other.poll())
            self.assertIsNone(self.process.poll())
        finally:
            other.terminate()
            try:
                other.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                other.kill()
                other.communicate()


if __name__ == "__main__":
    unittest.main()
