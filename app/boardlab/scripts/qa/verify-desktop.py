#!/usr/bin/env python3
"""Real X11 input checks. Run under Xvfb with xdotool on PATH."""

import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
BINARY = Path(os.environ.get("BOARDLAB_QA_BINARY", ROOT / "build/boardlab"))
CAPTURES = ROOT / "output/qa/round2"


class DesktopWorkflows(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="boardlab-desktop-qa-")
        self.addCleanup(self.temporary.cleanup)
        self.environment = dict(os.environ, QT_QPA_PLATFORM="xcb", QT_QUICK_BACKEND="software",
                                XDG_RUNTIME_DIR=self.temporary.name)
        self.process = None

    def tearDown(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.communicate()

    def xdo(self, *arguments):
        return subprocess.check_output(["xdotool", *map(str, arguments)],
                                       env=self.environment, text=True, timeout=10).strip()

    def launch(self, name, *arguments):
        CAPTURES.mkdir(parents=True, exist_ok=True)
        self.process = subprocess.Popen(
            [str(BINARY), "--framed", "--no-web", "--reduce-motion", "--window-size", "800x480",
             "--screenshot", str(CAPTURES / (name + ".png")), "--screenshot-delay", "2800", *arguments],
            env=self.environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.window = self.xdo("search", "--sync", "--onlyvisible", "--pid", self.process.pid).splitlines()[0]
        self.xdo("windowfocus", "--sync", self.window)
        time.sleep(0.25)

    def click(self, x, y):
        self.xdo("mousemove", "--window", self.window, x, y, "click", 1)

    def verify_capture(self):
        _, error = self.process.communicate(timeout=10)
        self.assertEqual(self.process.returncode, 0, error)
        for failure in ["TypeError", "ReferenceError", "Binding loop", "Unable to assign"]:
            self.assertNotIn(failure, error)

    def test_desktop_field_keeps_keyboard_hidden(self):
        self.launch("desktop-input", "--assert-hidden-object", "boardlab-virtual-keyboard",
                    "--assert-visible-object", "home-tool-search")
        self.click(320, 220)
        self.xdo("type", "monitor")
        self.verify_capture()

    def test_keyboard_toggle_reserves_content_space(self):
        self.launch("touch-input", "--assert-visible-object", "boardlab-virtual-keyboard",
                    "--assert-visible-object", "home-tool-search")
        self.click(760, 90)
        self.click(320, 220)
        self.verify_capture()

    def test_onscreen_enter_opens_search_result(self):
        self.launch("touch-enter", "--virtual-keyboard", "on",
                    "--assert-visible-object", "resource-monitor-app",
                    "--assert-hidden-object", "boardlab-virtual-keyboard")
        self.click(320, 220)
        self.xdo("type", "monitor")
        self.click(598, 430)
        self.verify_capture()

    def test_escape_closes_dropdown_before_leaving_page(self):
        self.environment.update(BOARDLAB_TEST_MODE="1", BOARDLAB_BOARD_PROFILE="topeet-rk3588")
        self.launch("dropdown-escape", "--open-tool", "UART",
                    "--open-dropdown", "uart-baud-dropdown",
                    "--assert-visible-object", "uart-work-surface",
                    "--assert-hidden-object", "uart-baud-dropdown-popup")
        time.sleep(0.8)
        self.xdo("key", "Escape")
        self.verify_capture()

    def test_reopen_page_and_escape_home(self):
        self.launch("return-to-home", "--assert-visible-object", "home-tool-Monitor",
                    "--assert-hidden-object", "resource-monitor-app")
        self.click(320, 220)
        self.xdo("type", "monitor")
        self.xdo("key", "Return")
        time.sleep(0.4)
        self.xdo("key", "alt+Left")
        time.sleep(0.1)
        self.xdo("key", "space")
        time.sleep(0.25)
        self.xdo("key", "Escape")
        self.xdo("key", "Escape")
        self.verify_capture()


if __name__ == "__main__":
    unittest.main()
