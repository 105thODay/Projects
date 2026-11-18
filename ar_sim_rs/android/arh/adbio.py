"""adb plumbing: running commands on phones, probing them, and running work on many at once."""

import json
import os
import re
import subprocess
import threading
from datetime import datetime, timezone
from pathlib import Path

REMOTE = "/data/local/tmp/ar"
HELPER_PACKAGE = "org.arsim.mcastlock"
HELPER_ACTIVITY = HELPER_PACKAGE + "/.MainActivity"
RESULTS = Path("ar_results")


def adb_program():
    return os.environ.get("ADB", "adb")


def adb(serial, *args, timeout=120, check=True):
    command = [adb_program()] + (["-s", serial] if serial else []) + list(args)
    result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    if check and result.returncode != 0:
        raise RuntimeError(f"{' '.join(command)} failed: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def shell(serial, command, timeout=120, check=True):
    return adb(serial, "shell", command, timeout=timeout, check=check)


def json_lines(text):
    """The JSON objects in a device's output; other lines are ignored."""
    objects = []
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("{"):
            try:
                objects.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return objects


def list_adb():
    """Every phone adb lists, with its state: device, unauthorized, offline, ..."""
    found = []
    for line in adb(None, "devices").splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 2:
            found.append((parts[0], parts[1]))
    return found


def parse_getprop(text):
    props = {}
    for match in re.finditer(r"^\[([^\]]+)\]: \[(.*)\]\s*$", text, re.MULTILINE):
        props[match.group(1)] = match.group(2)
    return props


def parse_free_mb(lines):
    """Free space from `df <path>`, in either toybox (1K-blocks ... Available) or old toolbox (Size Used Free) form."""
    if len(lines) < 2:
        return None
    header = lines[0].split()
    data = " ".join(lines[1:]).split()
    try:
        if "Available" in header:
            return int(data[header.index("Available")]) // 1024
        if "Free" in header:
            text = data[header.index("Free")]
            scale = {"K": 1 / 1024, "M": 1, "G": 1024, "T": 1024 * 1024}
            if text[-1] in scale:
                return int(float(text[:-1]) * scale[text[-1]])
            return int(text) // (1024 * 1024)
    except (ValueError, IndexError):
        return None
    return None


PROBE = ("getprop; echo __MEM__; head -n 1 /proc/meminfo; echo __DF__; (df /data/local/tmp/ar 2>/dev/null || df /data/local/tmp); "
         "echo __IP__; (ip -4 -o addr show wlan0 2>/dev/null || ifconfig wlan0 2>/dev/null); "
         "echo __WIFI__; dumpsys wifi 2>/dev/null | grep mWifiInfo | head -n 1; "
         "echo __CPU__; cat /sys/devices/system/cpu/cpu*/cpufreq/cpuinfo_max_freq 2>/dev/null")


class Phone:
    """What adb can learn about a phone without installing anything."""

    def __init__(self, serial):
        self.serial = serial
        text = shell(serial, PROBE, timeout=60, check=False)
        sections = {"props": []}
        current = "props"
        for line in text.splitlines():
            marker = line.strip()
            if marker.startswith("__") and marker.endswith("__"):
                current = marker.strip("_").lower()
                sections[current] = []
            else:
                sections[current].append(line.rstrip("\r"))
        props = parse_getprop("\n".join(sections["props"]))
        self.manufacturer = props.get("ro.product.manufacturer", "")
        self.model = props.get("ro.product.model", "") or serial
        self.release = props.get("ro.build.version.release", "")
        self.sdk = int(props.get("ro.build.version.sdk", "0") or 0)
        self.security_patch = props.get("ro.build.version.security_patch", "")
        abilist = props.get("ro.product.cpu.abilist", "")
        self.abis = [a for a in abilist.split(",") if a] or [
            a for a in (props.get("ro.product.cpu.abi", ""), props.get("ro.product.cpu.abi2", "")) if a]
        self.abi = self.abis[0] if self.abis else ""
        self.soc = (props.get("ro.soc.model") or props.get("ro.board.platform") or props.get("ro.hardware") or "")
        serialno = props.get("ro.serialno", "").strip()
        if not serialno or serialno.lower() in ("unknown", "0123456789abcdef"):
            serialno = shell(serial, "settings get secure android_id", check=False).strip() or serial
        self.phone_id = serialno
        mem = " ".join(sections.get("mem", []))
        match = re.search(r"(\d+)\s*kB", mem)
        self.ram_mb = int(match.group(1)) // 1024 if match else None
        self.free_mb = parse_free_mb([l for l in sections.get("df", []) if l.strip()])
        match = re.search(r"inet (?:addr:)?(\d+\.\d+\.\d+\.\d+)", " ".join(sections.get("ip", [])))
        self.ip = match.group(1) if match else ""
        wifi = " ".join(sections.get("wifi", []))
        self.wifi_mhz = int(m.group(1)) if (m := re.search(r"Frequency: (\d+)", wifi)) else None
        self.link_mbps = int(m.group(1)) if (m := re.search(r"Link speed: (\d+)", wifi)) else None
        self.rssi = int(m.group(1)) if (m := re.search(r"RSSI: (-?\d+)", wifi)) else None
        freqs = [int(x) for x in sections.get("cpu", []) if x.strip().isdigit()]
        self.cores = len(freqs) or None
        self.max_mhz = max(freqs) // 1000 if freqs else None

    def describe(self):
        return {"serial": self.serial, "phone_id": self.phone_id, "manufacturer": self.manufacturer,
                "model": self.model, "android": self.release, "sdk": self.sdk, "abi": self.abi,
                "abis": self.abis, "soc": self.soc, "security_patch": self.security_patch,
                "ram_mb": self.ram_mb, "free_mb": self.free_mb, "cores": self.cores, "max_mhz": self.max_mhz,
                "wlan_ip": self.ip, "wifi_mhz": self.wifi_mhz, "link_mbps": self.link_mbps, "rssi": self.rssi}


def parallel_soft(function, items):
    """Runs function(index, item) on a thread per item; returns (result, error) pairs in order."""
    outcomes = [(None, None)] * len(items)

    def work(index, item):
        try:
            outcomes[index] = (function(index, item), None)
        except Exception as error:
            outcomes[index] = (None, error)

    threads = [threading.Thread(target=work, args=(i, item)) for i, item in enumerate(items)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    return outcomes


def parallel(function, items):
    """As parallel_soft, but re-raises the first error."""
    outcomes = parallel_soft(function, items)
    for _, error in outcomes:
        if error:
            raise error
    return [result for result, _ in outcomes]


def new_run_dir(command):
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    path = RESULTS / f"{stamp}_{command}"
    path.mkdir(parents=True, exist_ok=False)
    return path


class Monitor:
    """Samples battery temperature and CPU clocks on every phone while a test runs."""

    def __init__(self, phones, out_dir, interval_s):
        self.phones = phones
        self.path = out_dir / "monitor.csv"
        self.interval = interval_s
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self.loop, daemon=True)

    def sample(self, phone):
        text = shell(phone.serial,
                     "dumpsys battery | grep -i temperature; "
                     "cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq 2>/dev/null",
                     timeout=20, check=False)
        temperature = ""
        freqs = []
        for line in text.splitlines():
            line = line.strip()
            if "temperature" in line.lower():
                digits = re.findall(r"-?\d+", line)
                if digits:
                    temperature = f"{int(digits[0]) / 10:.1f}"
            elif line.isdigit():
                freqs.append(str(int(line) // 1000))
        return temperature, "/".join(freqs)

    def loop(self):
        with open(self.path, "w") as out:
            out.write("utc,serial,battery_c,cpu_mhz\n")
            while not self.stop_event.is_set():
                stamp = datetime.now(timezone.utc).isoformat(timespec="seconds")
                for phone in self.phones:
                    try:
                        temperature, freqs = self.sample(phone)
                    except Exception:
                        temperature, freqs = "", ""
                    out.write(f"{stamp},{phone.serial},{temperature},{freqs}\n")
                out.flush()
                self.stop_event.wait(self.interval)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *exc):
        self.stop_event.set()
        self.thread.join(timeout=30)


def monitor_summary(path):
    """Per phone: battery temperature first, last and highest, and the highest CPU clock first and last."""
    rows = {}
    if not Path(path).is_file():
        return rows
    for line in Path(path).read_text().splitlines()[1:]:
        parts = line.split(",")
        if len(parts) < 4 or not parts[2]:
            continue
        freqs = [int(f) for f in parts[3].split("/") if f]
        rows.setdefault(parts[1], []).append((float(parts[2]), max(freqs) if freqs else 0))
    return {serial: {"battery_c_first": s[0][0], "battery_c_last": s[-1][0],
                     "battery_c_max": max(x[0] for x in s),
                     "cpu_max_mhz_first": s[0][1], "cpu_max_mhz_last": s[-1][1]}
            for serial, s in rows.items()}


def start_helper(phones):
    """Opens the helper app on every phone, keeps the screen on, and reports which confirm the locks."""
    def open_one(index, phone):
        shell(phone.serial, "svc power stayon true", check=False)
        shell(phone.serial, "input keyevent KEYCODE_WAKEUP", check=False)
        shell(phone.serial, "logcat -c", check=False)
        shell(phone.serial, f"am start -n {HELPER_ACTIVITY}", check=False)
    parallel_soft(open_one, phones)
    import time
    time.sleep(3)

    def confirm(index, phone):
        return "ar_sim locks held" in shell(phone.serial, "logcat -d -s ar_sim:I", check=False)
    return {phone.serial: bool(held) for phone, (held, _) in zip(phones, parallel_soft(confirm, phones))}
