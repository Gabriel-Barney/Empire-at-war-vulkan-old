#!/usr/bin/env python3
"""Missed nights of the nightly dev gate (wr0 t04).

The scheduled gate (tools\\gate_scheduled.cmd, task AlamoGateNightly) appends one line per run to
<main checkout>\\logs\\gate_history.txt, starting with cmd's %DATE% ("Fri 11/20/2026") or an ISO date. A night the task
did not run leaves no line, which nobody saw for a week in 2026-11 (the MSIX AppData redirection). This prints the
nights since the last line that have no line, so the next run can say so on its own history line:

  python -I tools/lint/gate_nights.py missed logs/gate_history.txt [--today YYYY-MM-DD]
      prints "missed N nights (FIRST..LAST)" or nothing when no night was missed; exit 0
  python -I tools/lint/gate_nights.py --selftest

A night is a calendar day: the 03:00 run of day D is night D. Days strictly between the last line's day and today count;
today does not (its run is the one asking). Standard library only.
"""

from __future__ import annotations

import datetime as dt
import re
import sys

_MDY = re.compile(r"\b(\d{1,2})/(\d{1,2})/(\d{4})\b")
_ISO = re.compile(r"\b(\d{4})-(\d{2})-(\d{2})\b")


def line_day(line: str) -> dt.date | None:
    """The day a history line was written: the first M/D/YYYY (cmd's %DATE% in the en-US locale) or YYYY-MM-DD."""
    m = _MDY.search(line)
    i = _ISO.search(line)
    if m and (not i or m.start() < i.start()):
        try:
            return dt.date(int(m.group(3)), int(m.group(1)), int(m.group(2)))
        except ValueError:
            return None
    if i:
        try:
            return dt.date(int(i.group(1)), int(i.group(2)), int(i.group(3)))
        except ValueError:
            return None
    return None


def missed_nights(lines: list[str], today: dt.date) -> list[dt.date]:
    days = [d for d in (line_day(x) for x in lines) if d is not None]
    if not days:
        return []
    last = max(days)
    return [last + dt.timedelta(days=k) for k in range(1, (today - last).days)]


def describe(missed: list[dt.date]) -> str:
    if not missed:
        return ""
    n = len(missed)
    span = missed[0].isoformat() if n == 1 else f"{missed[0].isoformat()}..{missed[-1].isoformat()}"
    return f"missed {n} night{'s' if n != 1 else ''} ({span})"


def selftest() -> int:
    hist = [
        "Thu 11/12/2026  3:06:37.37 e0b043fe gate exit 0",
        "Fri 11/20/2026  7:12:12.82 6d119d0c dev gate exit 1",
    ]
    checks = [
        ("gap of a week", describe(missed_nights(hist[:1], dt.date(2026, 11, 20))),
         "missed 7 nights (2026-11-13..2026-11-19)"),
        ("no gap", describe(missed_nights(hist, dt.date(2026, 11, 21))), ""),
        ("same day", describe(missed_nights(hist, dt.date(2026, 11, 20))), ""),
        ("one night", describe(missed_nights(hist, dt.date(2026, 11, 22))), "missed 1 night (2026-11-21)"),
        ("iso lines", describe(missed_nights(["2026-11-01 abc gate exit 0"], dt.date(2026, 11, 4))),
         "missed 2 nights (2026-11-02..2026-11-03)"),
        ("empty history", describe(missed_nights([], dt.date(2026, 11, 4))), ""),
        ("unparsable lines ignored", describe(missed_nights(["junk", hist[1]], dt.date(2026, 11, 21))), ""),
        ("out-of-order lines", describe(missed_nights([hist[1], hist[0]], dt.date(2026, 11, 22))),
         "missed 1 night (2026-11-21)"),
    ]
    bad = [(n, g, w) for n, g, w in checks if g != w]
    for n, g, w in bad:
        print(f"gate_nights selftest FAIL {n}: got {g!r}, want {w!r}")
    if bad:
        return 1
    print(f"gate_nights selftest: ok ({len(checks)} checks)")
    return 0


def main(argv: list[str]) -> int:
    if argv[:1] == ["--selftest"]:
        return selftest()
    if len(argv) < 2 or argv[0] != "missed":
        print(__doc__)
        return 2
    today = dt.date.today()
    if "--today" in argv:
        today = dt.date.fromisoformat(argv[argv.index("--today") + 1])
    try:
        with open(argv[1], encoding="utf-8", errors="replace") as f:
            lines = f.read().splitlines()
    except OSError:
        lines = []
    text = describe(missed_nights(lines, today))
    if text:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
