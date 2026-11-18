"""Shared report helpers: number formatting, the phone-condition table, and writing a run's report."""

import json
import math


def fmt(value, digits=1):
    if value is None or (isinstance(value, float) and math.isnan(value)):
        return "–"
    return f"{value:,.{digits}f}"


def add_monitor_lines(lines, phones, monitor):
    """phones: objects or dicts with serial and model."""
    if not monitor:
        return
    lines += ["", "## Phone condition during the run", "",
              "| Phone | Battery temperature, first to last (max) | Highest CPU clock, first to last |",
              "|---|---|---|"]
    for phone in phones:
        serial = phone["serial"] if isinstance(phone, dict) else phone.serial
        model = phone["model"] if isinstance(phone, dict) else phone.model
        m = monitor.get(serial)
        if m:
            lines.append(f"| {model} | {m['battery_c_first']:.1f} to {m['battery_c_last']:.1f} °C "
                         f"({m['battery_c_max']:.1f}) | {m['cpu_max_mhz_first']} to {m['cpu_max_mhz_last']} MHz |")


def finish(out, report, lines, name="report"):
    (out / f"{name}.json").write_text(json.dumps(report, indent=1, default=str))
    (out / f"{name}.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"\nsaved to {out}")
