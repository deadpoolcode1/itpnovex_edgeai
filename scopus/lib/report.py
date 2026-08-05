"""
Scopus system-test report model + HTML/PDF writer.

Mirrors the look of the per-device reports (edgeai/tests/run_tests.py and
V20_SDVR modular-tools.sh test-run) so the whole-system report sits next to
them stylistically: grouped rows, pass/fail/skip colouring, self-contained
HTML (no external assets) and a same-stem PDF via headless Chrome / wkhtmltopdf.
"""
from __future__ import annotations

import shutil
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Tuple

# ANSI colours for the live console run
C_RED = "\033[91m"; C_GRN = "\033[92m"; C_YEL = "\033[93m"
C_BLU = "\033[94m"; C_CYN = "\033[96m"; C_BOLD = "\033[1m"; C_RST = "\033[0m"


@dataclass
class TestResult:
    id: str
    desc: str
    status: str          # 'pass' | 'fail' | 'skip'
    reason: str = ""     # populated for fail / skip
    extra: str = ""      # optional info (timing, captured value, SoW ref)


@dataclass
class Suite:
    results: List[TestResult] = field(default_factory=list)
    groups: List[Tuple[int, str]] = field(default_factory=list)
    # device snapshot captured during prerequisites
    n6_fw: str = "?"
    n6_app: str = "?"
    n6_mode: str = "?"          # 'edgeai-app' | 'stock' | '?'
    modem_ver: str = "?"
    modem_cmds: str = "?"

    def group(self, title: str):
        self.groups.append((len(self.results), title))
        print(f"\n{C_BOLD}{C_BLU}── {title} ──{C_RST}")

    def add(self, r: TestResult):
        self.results.append(r)
        col = {"pass": C_GRN, "fail": C_RED, "skip": C_YEL}[r.status]
        suffix = f" — {r.reason}" if r.reason else ""
        extra = f"  {C_CYN}{r.extra}{C_RST}" if r.extra else ""
        print(f"  [{r.id:>7s}] {r.desc:<66s} {col}{r.status.upper():>4s}{C_RST}{suffix}{extra}")

    # convenience recorders -------------------------------------------------
    def ok(self, tid, desc, cond, reason="", extra=""):
        self.add(TestResult(tid, desc, "pass" if cond else "fail",
                            reason="" if cond else reason, extra=extra))
        return bool(cond)

    def skip(self, tid, desc, reason):
        self.add(TestResult(tid, desc, "skip", reason=reason))

    def passed(self): return sum(1 for r in self.results if r.status == "pass")
    def failed(self): return sum(1 for r in self.results if r.status == "fail")
    def skipped(self): return sum(1 for r in self.results if r.status == "skip")
    def total(self): return len(self.results)


_HTML_HEAD = """<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Scopus System Test Report</title>
<style>
  body { font-family: -apple-system, Arial, sans-serif; margin: 20px; background: #f5f5f5; }
  h1 { color: #333; } h2 { color: #555; margin-top: 20px; }
  .meta { color: #666; margin-bottom: 20px; line-height: 1.5em; }
  .summary { font-size: 1.3em; padding: 15px; border-radius: 8px; margin: 15px 0; }
  .summary.pass { background: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
  .summary.fail { background: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
  table { border-collapse: collapse; width: 100%; background: white; box-shadow: 0 1px 3px rgba(0,0,0,.12); margin-bottom: 20px; }
  th { background: #343a40; color: white; padding: 10px 12px; text-align: left; }
  td { padding: 8px 12px; border-bottom: 1px solid #dee2e6; vertical-align: top; }
  tr.group td { background: #e9ecef; font-weight: bold; padding: 10px 12px; }
  tr.pass td:last-child { color: #28a745; font-weight: bold; }
  tr.fail td:last-child { color: #dc3545; font-weight: bold; }
  tr.skip td:last-child { color: #ffc107; font-weight: bold; }
  td.extra, span.extra { color: #6c757d; font-size: .9em; }
  .footer { color: #999; margin-top: 30px; font-size: 0.9em; }
</style></head><body>
<h1>Scopus PoC — Whole-System Test Report</h1>
"""


def _esc(s: str) -> str:
    return (s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))


def write_report(out_path: Path, suite: Suite, runtime_s: int, meta: dict):
    total, passed, failed, skipped = (suite.total(), suite.passed(),
                                      suite.failed(), suite.skipped())
    rclass = "pass" if failed == 0 else "fail"
    rtext = "ALL TESTS PASSED" if failed == 0 else f"{failed} TEST(S) FAILED"

    group_at = dict(suite.groups)
    rows = []
    for i, r in enumerate(suite.results):
        if i in group_at:
            rows.append(f"<tr class='group'><td colspan='3'><b>{_esc(group_at[i])}</b></td></tr>")
        cell = r.status.upper()
        if r.reason:
            cell += f" — {_esc(r.reason)}"
        extra = f"<br><span class='extra'>{_esc(r.extra)}</span>" if r.extra else ""
        rows.append(f"<tr class='{r.status}'><td>{_esc(r.id)}</td>"
                    f"<td>{_esc(r.desc)}{extra}</td><td>{cell}</td></tr>")

    body = _HTML_HEAD + f"""<div class="meta">
  <b>Date:</b> {time.strftime('%Y-%m-%d %H:%M:%S')}<br>
  <b>System:</b> Scopus PoC — N6 Main CPU (camera/detection) + WP76 modem (SDVR app)<br>
  <b>N6 firmware:</b> {_esc(meta.get('n6_fw','?'))} &nbsp; <b>N6 app:</b> {_esc(meta.get('n6_app','?'))} &nbsp; <b>mode:</b> {_esc(meta.get('n6_mode','?'))}<br>
  <b>Modem SDVR:</b> {_esc(meta.get('modem_ver','?'))} ({_esc(meta.get('modem_cmds','?'))} AT cmds) &nbsp; <b>host:</b> {_esc(meta.get('host','?'))}<br>
  <b>N6 shell:</b> {_esc(meta.get('n6_tty','?'))} &nbsp; <b>Modem AT:</b> {_esc(meta.get('modem_tty','?'))} &nbsp; <b>Modem IP:</b> {_esc(meta.get('modem_ip','?'))}<br>
  <b>Runtime:</b> {runtime_s} seconds
</div>
<div class="summary {rclass}">
  <b>Result:</b> {rtext}
  &nbsp;—&nbsp; Total: {total} &nbsp;|&nbsp; Pass: {passed} &nbsp;|&nbsp; Fail: {failed} &nbsp;|&nbsp; Skip: {skipped}
</div>
<table>
<tr><th style="width:10%">Test ID</th><th style="width:75%">Description (SoW ref)</th><th style="width:15%">Result</th></tr>
{chr(10).join(rows)}
</table>
<div class="footer">Generated by scopus/run_scopus_tests.py — Kamacode Ltd.</div>
</body></html>
"""
    out_path.write_text(body, encoding="utf-8")
    _maybe_pdf(out_path)


def _maybe_pdf(html_path: Path):
    """Render a same-stem PDF. Prefer wkhtmltopdf, else headless Chrome —
    same fallback chain the two per-device suites use."""
    pdf = html_path.with_suffix(".pdf")
    wk = shutil.which("wkhtmltopdf")
    if wk:
        try:
            subprocess.run([wk, "--quiet", str(html_path), str(pdf)],
                           capture_output=True, timeout=60)
            if pdf.exists():
                print(f"  PDF: {pdf}")
                return
        except Exception:
            pass
    chrome = (shutil.which("google-chrome") or shutil.which("google-chrome-stable")
              or shutil.which("chromium") or shutil.which("chromium-browser"))
    if not chrome:
        print("  (no wkhtmltopdf / chrome on PATH — HTML report only)")
        return
    try:
        subprocess.run([chrome, "--headless", "--disable-gpu", "--no-sandbox",
                        f"--print-to-pdf={pdf}", "--no-pdf-header-footer",
                        f"file://{html_path.resolve()}"],
                       capture_output=True, timeout=60)
        if pdf.exists():
            print(f"  PDF: {pdf}")
    except Exception as e:
        print(f"  PDF render failed: {e}")
