"""Running one multicast session across a set of phones and pulling its logs."""

import json

from .adbio import REMOTE, Monitor, adb, json_lines, parallel, shell


def net_command(params, index, phone, count, out_name):
    words = [
        "net", f"id={index}", f"n={count}", f"group={params['group']}", f"iface={phone.ip}",
        f"duration_s={params['duration']}", f"warmup_s={params['warmup']}", f"state_hz={params['state_hz']}",
        f"summary_hz={params['summary_hz']}", f"input_hz={params['input_hz']}",
        f"catchup_at_s={params['catchup_at']}", f"blackout_ms={params['blackout_ms']}",
        f"seed={params['seed']}", f"out={REMOTE}/{out_name}",
    ]
    if params.get("loop"):
        words.append("loop=1")  # host emulation only: every process shares one machine
    return " ".join(words)


def run_session(phones, out, name, params, monitor_interval):
    """Runs `ar_device net` on every phone at once (phone i gets id i; phone 0 leads), then pulls the logs.

    Writes <name>_stdout_<i>.jsonl, <name>_<i>/tx_<i>.csv and rx_<i>.csv, and <name>_meta.json under out.
    """
    count = len(phones)
    for phone in phones:
        shell(phone.serial, f"rm -rf {REMOTE}/{name}", check=False)

    def run_one(index, phone):
        text = shell(phone.serial, f"cd {REMOTE} && ./ar_device {net_command(params, index, phone, count, name)}",
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
            "state_hz": params["state_hz"], "summary_hz": params["summary_hz"], "catchup_at_s": params["catchup_at"],
            "blackout_ms": params["blackout_ms"], "group": params["group"]}
    (out / f"{name}_meta.json").write_text(json.dumps(meta, indent=1))
    return meta
