#!/usr/bin/env python3
"""Capture each lazy page without starting hardware tests. Run under Xvfb."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BINARY = Path(os.environ.get("BOARDLAB_QA_BINARY", ROOT / "build/boardlab"))
CAPTURES = ROOT / "output/qa/round2/surfaces"
APPS = {
    "Storage": "storage-test-app",
    "Diagnostics": "diagnostics-app",
    "Environment": "environment-test-app",
    "Keys": "key-test-app",
    "GPU": "gpu-test-app",
    "Monitor": "resource-monitor-app",
    "NPU YOLO": "npu-yolo-demo-app",
    "WiFi": "wifi-test-app",
    "RK Desk": "rk-desk-app",
    "Qt Web": "web-console-app",
}
TOOLS = ["GPIO", "UART", "Ethernet", "CAN", "Cellular", "Camera", "HDMIIN",
         "Watchdog", "Music", "Buzzer", "Fan", "LED", "Backlight", "Files"]
QML_ERRORS = ["TypeError", "ReferenceError", "Binding loop", "Unable to assign",
              "is not a type", "Cannot assign to non-existent property", "Cannot open:"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sizes", nargs="+", default=["800x480", "1280x800", "480x800"])
    arguments = parser.parse_args()
    routes = [("home", [], "home-tool-search")]
    routes.extend((name.lower().replace(" ", "-"), ["--open-app", name], object_name)
                  for name, object_name in APPS.items())
    routes.extend((name.lower(), ["--open-tool", name], name.lower() + "-work-surface")
                  for name in TOOLS)
    CAPTURES.mkdir(parents=True, exist_ok=True)
    failed = []
    with tempfile.TemporaryDirectory(prefix="boardlab-surfaces-qa-") as runtime:
        environment = dict(os.environ, QT_QPA_PLATFORM="xcb", QT_QUICK_BACKEND="software",
                           XDG_RUNTIME_DIR=runtime, BOARDLAB_TEST_MODE="1",
                           BOARDLAB_BOARD_PROFILE="topeet-rk3588")
        for size in arguments.sizes:
            for name, route, object_name in routes:
                label = name + "-" + size
                command = [str(BINARY), "--framed", "--no-web", "--reduce-motion",
                           "--window-size", size, "--screenshot", str(CAPTURES / (label + ".png")),
                           "--screenshot-delay", "1500", "--assert-visible-object", object_name,
                           *route]
                try:
                    result = subprocess.run(command, env=environment, capture_output=True,
                                            text=True, timeout=15)
                    error = result.stderr
                    passed = result.returncode == 0 and not any(text in error for text in QML_ERRORS)
                except subprocess.TimeoutExpired as failure:
                    error = str(failure)
                    passed = False
                (CAPTURES / (label + ".log")).write_text(error)
                print(("PASS " if passed else "FAIL ") + label, flush=True)
                if not passed:
                    failed.append(label)
    total = len(routes) * len(arguments.sizes)
    print(f"{total - len(failed)}/{total} route captures passed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
