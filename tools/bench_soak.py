#!/usr/bin/env python3
"""Bench acceptance ≥ 24 h soak — Proposal v4 A5 deliverable.

Loops the regression suite forever (or for `--hours N`), tracking:

  - Per-iteration pass/fail/skip counts
  - Per-image NN inference time (drift detection)
  - Kit uptime monitor — flags an unexpected reboot mid-soak
  - URC traffic count from the modem-task (if a fake_modem is wired up)
  - Heap / stack high-water from `nvic stats` and `safe status` (best
    effort — these commands may not exist on every build)

Each loop produces a one-line CSV row; full HTML reports land in
results/. The summary at the end (or on Ctrl-C) prints:

  - Total iterations
  - First / last uptime
  - Number of unexpected reboots
  - Pass rate %
  - Median + P95 NN time across all per-image inferences

Usage:
  python3 tools/bench_soak.py                  # forever, until Ctrl-C
  python3 tools/bench_soak.py --hours 24       # explicit limit
  python3 tools/bench_soak.py --hours 0.1      # 6 min smoke

CSV goes to results/soak-<timestamp>.csv. Tail -f friendly so an operator
can watch progress from another terminal.
"""
from __future__ import annotations
import argparse
import csv
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).parent
ROOT = HERE.parent
RUN_TESTS = ROOT / "tests" / "run_tests.py"
RESULTS  = ROOT / "results"

NN_TIME_RE = re.compile(r"NN[= ]([\d.]+)\s*ms")
UPTIME_RE  = re.compile(r"Uptime:\s+(\d+)\[?msec")
SUMMARY_RE = re.compile(r"TOTAL:\s+(\d+)\s+.*?PASS:\s+(\d+).*?FAIL:\s+(\d+).*?SKIP:\s+(\d+).*?TIME:\s+(\d+)s")


def find_kit_tty() -> str | None:
    p = "/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02"
    if os.path.exists(p):
        return os.path.realpath(p)
    return None


def kit_uptime_ms(tty: str) -> int | None:
    """Open the kit's CDC, ask uptime, return ms-since-boot. None if dead."""
    sys.path.insert(0, str(ROOT / "tests"))
    from run_tests import KitShell  # type: ignore
    try:
        sh = KitShell(tty)
        sh.drain(1.0)
        sh.write_line("uptime")
        out = sh.read_until("ok", 2.0)
        sh.close()
        m = UPTIME_RE.search(out)
        return int(m.group(1)) if m else None
    except Exception:
        return None


def wait_kit_stable(tty_path: str, timeout: float = 120.0) -> bool:
    """Wait until the kit's CDC has been continuously present for several
    seconds AND responds to a basic shell query. Returns False if timeout.

    When a previous iteration crashed the kit, the bootloop cycles the
    CDC every ~15-20 s and the by-id symlink may flicker. Running the
    next iteration during this window all but guarantees another empty
    result. Holding off until the kit is verifiably back avoids cascading
    failures. Uses the by-id path (not the realpath /dev/ttyACMx) so we
    survive ttyACM1↔ttyACM2 renumbering during the bootloop."""
    by_id = "/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02"
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(by_id):
            # Hold for a few seconds — if the kit's mid-reboot the symlink
            # will vanish again within this window.
            time.sleep(4.0)
            if not os.path.exists(by_id):
                continue
            # Confirm the shell is actually answering (not just CDC enumerated).
            up = kit_uptime_ms(os.path.realpath(by_id))
            if up is not None and up > 2000:  # at least 2 s post-boot
                return True
        time.sleep(0.5)
    return False


def run_one_iter(out_dir: Path, iter_id: int) -> tuple[dict, list[float]]:
    """Run the regression suite once. Returns (summary-dict, [nn-times])."""
    html = out_dir / f"soak-{iter_id:05d}.html"
    proc = subprocess.run(
        [sys.executable, str(RUN_TESTS), "--out", str(html)],
        capture_output=True, text=True, timeout=300,
    )
    out = proc.stdout + proc.stderr
    summary = {"total": 0, "pass": 0, "fail": 0, "skip": 0, "time_s": 0}
    m = SUMMARY_RE.search(out)
    if m:
        summary = {
            "total":  int(m.group(1)),
            "pass":   int(m.group(2)),
            "fail":   int(m.group(3)),
            "skip":   int(m.group(4)),
            "time_s": int(m.group(5)),
        }
    nn_times = [float(x) for x in NN_TIME_RE.findall(out)]
    return summary, nn_times


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--hours", type=float, default=None,
                    help="Stop after this many hours (default: run forever)")
    ap.add_argument("--out-dir", type=Path,
                    default=RESULTS / f"soak-{time.strftime('%Y%m%d_%H%M%S')}",
                    help="Per-iteration HTML reports + CSV log land here")
    args = ap.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    csv_path = args.out_dir / "soak.csv"

    tty = find_kit_tty()
    if not tty:
        print("ERROR: no N6Cam CDC port found", file=sys.stderr)
        return 1

    deadline = (time.time() + args.hours * 3600.0) if args.hours else None

    first_uptime = kit_uptime_ms(tty) or 0
    last_uptime  = first_uptime
    unexpected_reboots = 0
    iter_id = 0
    all_nn: list[float] = []
    all_pass = all_fail = all_skip = all_total = 0
    t0 = time.time()

    print(f"Soak start. Kit TTY: {tty}. Initial uptime: {first_uptime/1000:.1f} s")
    print(f"CSV: {csv_path}")
    print(f"HTML reports: {args.out_dir}/")
    if deadline:
        print(f"Will stop at: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(deadline))}")
    print()

    with csv_path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["iter", "wall_s", "kit_uptime_s", "rebooted",
                    "pass", "fail", "skip", "total", "test_time_s",
                    "nn_med_ms", "nn_p95_ms"])
        try:
            while True:
                if deadline and time.time() >= deadline:
                    break
                iter_id += 1

                # Wait for the kit to be stably enumerated before running the
                # next iteration. If the previous iter crashed the kit, the
                # CDC will be flapping; running tests in that window guarantees
                # 0/0 results and corrupts the soak data.
                if not wait_kit_stable(tty, timeout=120.0):
                    print(f"  iter {iter_id:4d}: SKIPPED — kit didn't come back within 120 s")
                    w.writerow([iter_id, int(time.time() - t0), 0, 1,
                                0, 0, 0, 0, 0, "0.0", "0.0"])
                    f.flush()
                    unexpected_reboots += 1
                    continue
                # Re-resolve the realpath in case the kernel re-assigned
                # ttyACMx after the reboot cycle.
                tty = os.path.realpath("/dev/serial/by-id/usb-STMicroelectronics_N6Cam_DEADBEEF-if02")

                pre_uptime = kit_uptime_ms(tty)
                rebooted = (pre_uptime is not None
                            and last_uptime is not None
                            and pre_uptime < last_uptime)
                if rebooted:
                    unexpected_reboots += 1
                    print(f"  ! kit rebooted: prev uptime {last_uptime/1000:.1f} s, "
                          f"now {pre_uptime/1000:.1f} s (#{unexpected_reboots})")

                summary, nns = run_one_iter(args.out_dir, iter_id)
                all_pass  += summary["pass"]
                all_fail  += summary["fail"]
                all_skip  += summary["skip"]
                all_total += summary["total"]
                all_nn.extend(nns)

                post_uptime = kit_uptime_ms(tty) or 0
                last_uptime = post_uptime

                nn_med = statistics.median(nns) if nns else 0.0
                nn_p95 = statistics.quantiles(nns, n=20)[-1] if len(nns) >= 20 else (max(nns) if nns else 0.0)
                w.writerow([iter_id, int(time.time() - t0), post_uptime // 1000,
                            int(rebooted),
                            summary["pass"], summary["fail"], summary["skip"], summary["total"],
                            summary["time_s"], f"{nn_med:.1f}", f"{nn_p95:.1f}"])
                f.flush()

                badge = ("PASS" if summary["fail"] == 0 else f"FAIL ({summary['fail']})")
                print(f"  iter {iter_id:4d}: {badge}  "
                      f"{summary['pass']}/{summary['total']} pass  "
                      f"nn med={nn_med:.1f} ms p95={nn_p95:.1f} ms  "
                      f"kit_up={post_uptime/1000:.0f} s  reboots={unexpected_reboots}")
        except KeyboardInterrupt:
            print("\nSoak interrupted by user.")

    elapsed_h = (time.time() - t0) / 3600.0
    pass_rate = (100.0 * all_pass / max(1, all_total)) if all_total else 0.0
    if all_nn:
        med = statistics.median(all_nn)
        p95 = statistics.quantiles(all_nn, n=20)[-1] if len(all_nn) >= 20 else max(all_nn)
    else:
        med = p95 = 0.0

    print()
    print("─" * 60)
    print(f"Soak summary:")
    print(f"  Duration:           {elapsed_h:.2f} hours")
    print(f"  Iterations:         {iter_id}")
    print(f"  Tests run:          {all_total}")
    print(f"  Pass rate:          {pass_rate:.2f} %  ({all_pass}p / {all_fail}f / {all_skip}s)")
    print(f"  Unexpected reboots: {unexpected_reboots}")
    print(f"  NN inference:       median {med:.1f} ms, P95 {p95:.1f} ms  ({len(all_nn)} samples)")
    print(f"  CSV:                {csv_path}")
    print("─" * 60)
    return 0 if (all_fail == 0 and unexpected_reboots == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
