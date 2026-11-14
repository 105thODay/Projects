#!/usr/bin/env python3
"""Print the operating-point rows of a summary CSV as one compact table per point.

Usage: python tools/point_table.py ar_summary_200.csv [ar_summary_100.csv ...]
Each cell: share of obligations late or missing, worst L, state age P95, and seeds passing service.
"""
import csv
import sys

for path in sys.argv[1:]:
    rows = [r for r in csv.DictReader(open(path)) if r["group"].startswith("unicast")]
    for group in dict.fromkeys(r["group"] for r in rows):
        print(f"\n### {group}\n")
        print("| test | late or missing | worst L ms | state age P95 ms | service |")
        print("|---|---|---|---|---|")
        for r in (r for r in rows if r["group"] == group):
            ob = int(r["obligations"])
            bad = ob - int(r["on_time"])
            share = "0" if bad == 0 else (f"{bad} of {ob}" if 100 * bad / ob < 0.1 else f"{100 * bad / ob:.1f} %")
            print(f"| {r['label']} | {share} | {float(r['L_max']):.0f} | {r['state_age_p95']} | {r['service_pass']} |")
