#!/usr/bin/env python3
"""A stand-in for adb that emulates phones on this machine, to test harness.py end to end.

Each fake phone is a folder under $FAKE_ADB_ROOT/<serial>/. /data/local/tmp/ar in a command or a
push/pull path maps into it, and `shell` runs the command with bash locally, with the phone's own
getprop, dumpsys, settings, ip, svc, am, input and logcat first on PATH, so a command written for a
phone runs unchanged. The serial's prefix picks the phone it pretends to be:

  s5-*      Galaxy S5, Android 6.0.1 (API 23), armeabi-v7a
  px-*      Pixel 8, Android 14 (API 34), arm64-v8a
  beam-*    XREAL Beam Pro, Android 14 (API 34), arm64-v8a
  old-*     Galaxy S4, Android 5.0 (API 21): below the harness floor
  x86-*     an x86_64 tablet: no ARM build for it
  deaf-*    Pixel 8 whose helper app never confirms the multicast lock
  unauth-*  listed by adb as unauthorized

  FAKE_ADB_ROOT=/tmp/fakephones FAKE_ADB_PHONES="s5-1,px-2,beam-3" ADB=tools/fake_adb.py python3 harness.py devices
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(os.environ.get("FAKE_ADB_ROOT", "/tmp/fakephones"))
PHONES = [p for p in os.environ.get("FAKE_ADB_PHONES", "s5-1,px-2").split(",") if p]
PROFILES = {
    "s5": ("samsung", "SM-G900V", "6.0.1", "23", "armeabi-v7a,armeabi", "msm8974"),
    "px": ("Google", "Pixel 8", "14", "34", "arm64-v8a", "Tensor G3"),
    "beam": ("XREAL", "X4000", "14", "34", "arm64-v8a,armeabi-v7a,armeabi", "SM6450"),
    "old": ("samsung", "SGH-M919", "5.0.1", "21", "armeabi-v7a,armeabi", "msm8960"),
    "x86": ("Generic", "x86 Tablet", "11", "30", "x86_64,x86", "x86"),
    "deaf": ("Google", "Pixel 8", "14", "34", "arm64-v8a", "Tensor G3"),
}


def profile(serial):
    return PROFILES[serial.split("-")[0]]


def phone_dir(serial):
    path = ROOT / serial
    (path / "ar").mkdir(parents=True, exist_ok=True)
    return path


def local(serial, text):
    return text.replace("/data/local/tmp/ar", str(phone_dir(serial) / "ar"))


def tools(serial):
    """Writes this phone's stand-in Android tools and returns their folder."""
    maker, model, release, sdk, abis, soc = profile(serial)
    index = PHONES.index(serial)
    folder = phone_dir(serial) / "bin"
    folder.mkdir(exist_ok=True)
    held = "" if serial.startswith("deaf") else "echo 'I ar_sim  : ar_sim locks held: multicast, wifi mode 3, wake'"
    scripts = {
        "getprop": "\n".join([
            f"echo '[ro.product.manufacturer]: [{maker}]'", f"echo '[ro.product.model]: [{model}]'",
            f"echo '[ro.build.version.release]: [{release}]'", f"echo '[ro.build.version.sdk]: [{sdk}]'",
            f"echo '[ro.product.cpu.abilist]: [{abis}]'", f"echo '[ro.product.cpu.abi]: [{abis.split(',')[0]}]'",
            f"echo '[ro.board.platform]: [{soc}]'", f"echo '[ro.serialno]: [FAKE{index:04d}{serial.upper()}]'",
            "echo '[ro.build.version.security_patch]: [2026-10-01]'"]),
        "dumpsys": "\n".join([
            'case "$1" in',
            f"  battery) echo '  temperature: {300 + index * 7}';;",
            "  wifi) echo 'mWifiInfo SSID: \"testnet\", BSSID: 02:00:00:00:00:00, RSSI: -48, Link speed: 433Mbps, Frequency: 5180MHz, Net ID: 0';;",
            "esac"]),
        "settings": "echo 0123abcd",
        "ip": "echo '3: lo    inet 127.0.0.1/8 scope host lo'",
        "ifconfig": "echo 'lo inet addr:127.0.0.1'",
        "logcat": f'case "$*" in *-d*) {held or "true"};; esac',
        "svc": "true", "am": "true", "input": "true",
    }
    for name, body in scripts.items():
        path = folder / name
        path.write_text("#!/bin/bash\n" + body + "\n")
        path.chmod(0o755)
    return folder


def shell(serial, command):
    env = dict(os.environ, PATH=f"{tools(serial)}:{os.environ['PATH']}")
    command = local(serial, command).replace("cat /sys/devices/system/cpu/cpu*/cpufreq/",
                                             "printf '1800000\\n2400000\\n' #")
    result = subprocess.run(["bash", "-c", command], capture_output=True, text=True, env=env)
    sys.stdout.write(result.stdout)
    sys.stdout.write(result.stderr)
    return result.returncode


def main():
    args = sys.argv[1:]
    serial = None
    if args[:1] == ["-s"]:
        serial, args = args[1], args[2:]
    command = args[0]
    if command == "devices":
        print("List of devices attached")
        for phone in PHONES:
            print(f"{phone}\t{'unauthorized' if phone.startswith('unauth') else 'device'}")
        return 0
    if command == "shell":
        return shell(serial, " ".join(args[1:]))
    if command == "push":
        target = Path(local(serial, args[2]))
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(args[1], target)
        return 0
    if command == "pull":
        source = Path(local(serial, args[1]))
        if not source.is_file():
            print(f"adb: error: remote object '{args[1]}' does not exist", file=sys.stderr)
            return 1
        shutil.copy(source, args[2])
        return 0
    if command in ("install", "uninstall", "tcpip", "connect"):
        return 0
    print(f"fake adb: unsupported {args}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
