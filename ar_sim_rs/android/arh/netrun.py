"""Running one multicast session across a set of phones and pulling its logs."""

import json

from .adbio import REMOTE, Monitor, adb, json_lines, parallel, shell


def participant_uid(phone):
    """A stable 8-hex-digit participant identifier from the phone's hardware serial (FNV-1a, 32 bits)."""
    value = 0x811C9DC5
    for byte in phone.phone_id.encode():
        value = ((value ^ byte) * 0x01000193) & 0xFFFFFFFF
    return f"{value:08x}"


def roles(params, count):
    """Per phone: (role words) for a session run. Phone 0 leads; the others join at the start, a
    fraction of a second apart, except that with three or more phones the last joins late and
    phone 1 leaves and rejoins, so every run exercises catch-up and readmission."""
    plan = []
    for index in range(count):
        if index == 0:
            plan.append(["role=leader"])
            continue
        words = ["role=member", f"join_at_s={0.3 * index:.1f}"]
        if count >= 3 and index == count - 1 and params.get("late_join_at", -1) >= 0:
            words = ["role=member", f"join_at_s={params['late_join_at']}"]
        if count >= 3 and index == 1 and params.get("leave_at", -1) >= 0:
            words += [f"leave_at_s={params['leave_at']}", f"rejoin_at_s={params['rejoin_at']}"]
        plan.append(words)
    return plan


def net_command(params, index, phone, count, out_name, plan=None):
    words = [
        "net", f"group={params['group']}", f"iface={phone.ip}",
        f"duration_s={params['duration']}", f"warmup_s={params['warmup']}", f"state_hz={params['state_hz']}",
        f"summary_hz={params['summary_hz']}", f"input_hz={params['input_hz']}",
        f"blackout_ms={params['blackout_ms']}", f"seed={params['seed']}", f"out={REMOTE}/{out_name}",
    ]
    if params.get("mode") == "session":
        words += [f"log_id={index}", f"uid={participant_uid(phone)}", f"session={params['session']}",
                  f"marker_hz={params['marker_hz']}", f"delete_hz={params['delete_hz']}",
                  f"close_at_s={max(params['duration'] - 3, 1)}"] + plan[index]
    else:
        words += [f"id={index}", f"n={count}"]
    if params.get("loop"):
        words.append("loop=1")  # host emulation only: every process shares one machine
    return " ".join(words)


def run_session(phones, out, name, params, monitor_interval):
    """Runs `ar_device net` on every phone at once (phone i gets id i; phone 0 leads), then pulls the logs.

    Writes <name>_stdout_<i>.jsonl, <name>_<i>/tx_<i>.csv and rx_<i>.csv, and <name>_meta.json under out.
    """
    count = len(phones)
    plan = roles(params, count) if params.get("mode") == "session" else None
    for phone in phones:
        shell(phone.serial, f"rm -rf {REMOTE}/{name}", check=False)

    def run_one(index, phone):
        text = shell(phone.serial, f"cd {REMOTE} && ./ar_device {net_command(params, index, phone, count, name, plan)}",
                     timeout=params["duration"] + 120, check=False)
        (out / f"{name}_stdout_{index}.jsonl").write_text(text)
        return json_lines(text)

    with Monitor(phones, out, monitor_interval):
        results = parallel(run_one, phones)
    for index, phone in enumerate(phones):
        target = out / f"{name}_{index}"
        target.mkdir(exist_ok=True)
        for log in (f"tx_{index}.csv", f"rx_{index}.csv"):
            adb(phone.serial, "pull", f"{REMOTE}/{name}/{log}", str(target / log), timeout=300, check=False)
    meta = {"phones": [p.describe() for p in phones], "stdout": results, "name": name,
            "duration_s": params["duration"], "warmup_s": params["warmup"], "input_hz": params["input_hz"],
            "state_hz": params["state_hz"], "summary_hz": params["summary_hz"],
            "blackout_ms": params["blackout_ms"], "group": params["group"], "mode": params.get("mode", "fixed"),
            "plan": [" ".join(words) for words in plan] if plan else None}
    (out / f"{name}_meta.json").write_text(json.dumps(meta, indent=1))
    return meta
