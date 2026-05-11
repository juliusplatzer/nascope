#!/usr/bin/env python3
"""
reader/notams/scrape.py — fetch NOTAMs from notams.online, filter for
active runway/taxiway closures, and emit a compact JSON payload the C++
scope consumes directly via stdout (no on-disk cache).

Output schema:
    {
      "icao": "KSFO",
      "fetchedAt": "...",
      "rwyClosures": ["01L/19R", ...],
      "closedAreas": [
        {"id": "F1"},
        {"id": "C", "btnFrom": "RWY 01L/19R", "btnTo": "TWY L"}
      ],
      "restrictionAreas": [
        {
          "id": "A",
          "btnFrom": "GATE D7",
          "btnTo": "GATE D9",
          "condition": "ACFT WINGSPAN MORE THAN 118FT"
        }
      ]
    }

`closedAreas` / `restrictionAreas` are consumed by the C++ scope for supported
taxiway temp areas.

Usage:
    python3 tools/notams/scrape.py KSFO --output -        # stdout (scope consumes this)
    python3 tools/notams/scrape.py KJFK --output kjfk.json # local inspection

Requirements: pip install selenium webdriver-manager
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from datetime import datetime, timedelta, timezone

from selenium import webdriver
from selenium.common.exceptions import TimeoutException
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.chrome.service import Service
from selenium.webdriver.common.by import By
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.support.ui import WebDriverWait
from webdriver_manager.chrome import ChromeDriverManager


BASE_URL = "https://notams.online/icao/{icao}"
WAIT_TIMEOUT = 30
EXTRA_WAIT = 3.0
FETCH_ATTEMPTS = 2
LOAD_SELECTOR = ".notam-card, .raw-text"

# Trailing active period: YYMMDDHHMM-YYMMDDHHMM[EST] or ...-PERM[EST].
# Examples:
#   2604040605-2605311200
#   2604301600-2605071522EST
#   2503191622-PERM
PERIOD_RE = re.compile(r"(\d{10})-(\d{10}|PERM)(EST)?\s*$")

# Closure body patterns. We look inside the NOTAM body (after stripping the
# trailing period). The endpoints in BTN closures are typically two-token
# like "RWY 01L/19R", "TWY L", or "GATE D7"; we capture those verbatim so
# the C++ side can do exact lookups against the surface JSON later.
CONDITION_SUFFIX = r"(?:\s+TO\s+(.+))?"
AREA_ENDPOINT = r"(?:RWY|TWY|GATE)\s+\S+"
RWY_RE = re.compile(rf"\bRWY\s+(\S+)\s+CLSD{CONDITION_SUFFIX}\b")
TWY_BTN_RE = re.compile(
    rf"\bTWY\s+(\S+)\s+BTN\s+({AREA_ENDPOINT})\s+AND\s+({AREA_ENDPOINT})\s+CLSD{CONDITION_SUFFIX}\b"
)
TWY_PLAIN_RE = re.compile(rf"\bTWY\s+(\S+)\s+CLSD{CONDITION_SUFFIX}\b")


def build_driver() -> webdriver.Chrome:
    options = Options()
    options.add_argument("--headless=new")
    options.add_argument("--window-size=1280,900")
    options.add_argument(
        "user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/124.0.0.0 Safari/537.36"
    )
    service = Service(ChromeDriverManager().install())
    return webdriver.Chrome(service=service, options=options)


def fetch_raw_notams(icao: str) -> list[str]:
    url = BASE_URL.format(icao=icao.upper())
    print(f"[notams] opening {url}", file=sys.stderr)

    for attempt in range(1, FETCH_ATTEMPTS + 1):
        driver = build_driver()
        try:
            driver.get(url)
            try:
                WebDriverWait(driver, WAIT_TIMEOUT).until(
                    EC.presence_of_element_located((By.CSS_SELECTOR, LOAD_SELECTOR))
                )
            except TimeoutException:
                print(
                    f"[notams] timed out waiting for {LOAD_SELECTOR} "
                    f"(attempt {attempt}/{FETCH_ATTEMPTS})",
                    file=sys.stderr,
                )
                continue

            time.sleep(EXTRA_WAIT)
            # `.raw-text` divs are display:none, so .text() doesn't return their
            # contents under Selenium — read textContent via JS instead.
            raw = driver.execute_script(
                "return Array.from(document.querySelectorAll('.raw-text'))"
                "    .map(el => el.textContent.trim())"
                "    .filter(t => t.length > 0);"
            )
            if raw:
                return raw

            print(
                f"[notams] no .raw-text content found "
                f"(attempt {attempt}/{FETCH_ATTEMPTS})",
                file=sys.stderr,
            )
        finally:
            driver.quit()

    return []


def parse_period(text: str) -> tuple[datetime, datetime | None] | None:
    """Returns (start, end) where end=None means PERM/open-ended.

    Both datetimes are in UTC. EST suffix is converted to UTC by adding 5h
    (we don't bother with EDT — the FAA stamps EST year-round in NOTAMs).
    """
    m = PERIOD_RE.search(text)
    if not m:
        return None
    start_str, end_str, est = m.group(1), m.group(2), bool(m.group(3))

    def parse_one(s: str) -> datetime:
        dt = datetime.strptime(s, "%y%m%d%H%M")
        if est:
            return (dt + timedelta(hours=5)).replace(tzinfo=timezone.utc)
        return dt.replace(tzinfo=timezone.utc)

    start = parse_one(start_str)
    end = None if end_str == "PERM" else parse_one(end_str)
    return (start, end)


def is_active(period: tuple[datetime, datetime | None], now: datetime) -> bool:
    start, end = period
    if now < start:
        return False
    return end is None or now <= end


def has_multiple_closures(body: str) -> bool:
    """Conservative check for NOTAMs with comma-separated taxiway lists across
    multiple BTNs (e.g. KSFO NOTAM 47). We skip+log those; they need richer
    parsing than v1 supports."""
    # Two or more BTN tokens, or commas between two TWY tokens, indicates
    # a multi-segment closure that this v1 doesn't handle.
    if body.count(" BTN ") > 1:
        return True
    if re.search(r"TWY\s+\S+,", body):
        return True
    return False


def normalize_condition(raw: str | None) -> str | None:
    if not raw:
        return None
    return " ".join(raw.split())


def area_entry(cl: dict) -> dict:
    entry = {"id": cl["id"]}
    if "btnFrom" in cl:
        entry["btnFrom"] = cl["btnFrom"]
        entry["btnTo"] = cl["btnTo"]
    if "condition" in cl:
        entry["condition"] = cl["condition"]
    return entry


def classify_closure(body: str) -> dict | None:
    """Returns a dict describing the closure, or None if the body isn't a
    simple runway / taxiway closure we render today."""
    if has_multiple_closures(body):
        return None

    rwy = RWY_RE.search(body)
    if rwy:
        entry = {"kind": "rwy", "id": rwy.group(1)}
        condition = normalize_condition(rwy.group(2))
        if condition:
            entry["condition"] = condition
        return entry

    btn = TWY_BTN_RE.search(body)
    if btn:
        entry = {
            "kind": "twy",
            "id": btn.group(1),
            "btnFrom": " ".join(btn.group(2).split()),  # collapse internal whitespace
            "btnTo":   " ".join(btn.group(3).split()),
        }
        condition = normalize_condition(btn.group(4))
        if condition:
            entry["condition"] = condition
        return entry

    twy = TWY_PLAIN_RE.search(body)
    if twy:
        entry = {"kind": "twy", "id": twy.group(1)}
        condition = normalize_condition(twy.group(2))
        if condition:
            entry["condition"] = condition
        return entry

    return None


def filter_closures(raw: list[str], now: datetime) -> tuple[list[str], list[dict], list[dict], list[str]]:
    """Returns (rwyClosures, closedAreas, restrictionAreas, skipped).

    `skipped` is a log of NOTAM bodies we dropped as multi-segment or
    unrecognised.
    """
    rwy_set: set[str] = set()
    closed_area_list: list[dict] = []
    restriction_area_list: list[dict] = []
    closed_area_seen: set[str] = set()       # dedupe key: "id|btnFrom|btnTo"
    restriction_area_seen: set[str] = set()  # dedupe key: "id|btnFrom|btnTo|condition"
    skipped: list[str] = []

    for text in raw:
        period = parse_period(text)
        if period is None or not is_active(period, now):
            continue

        # Strip the period off the end so the body parsers don't see it.
        body = PERIOD_RE.sub("", text).strip()
        cl = classify_closure(body)
        if cl is None:
            if "CLSD" in body:
                skipped.append(body)
            continue

        if "condition" in cl:
            entry = area_entry(cl)
            key = (
                f"{entry['id']}|{entry.get('btnFrom', '')}|"
                f"{entry.get('btnTo', '')}|{entry.get('condition', '')}"
            )
            if key not in restriction_area_seen:
                restriction_area_seen.add(key)
                restriction_area_list.append(entry)
            continue

        if cl["kind"] == "rwy":
            rwy_set.add(cl["id"])
        else:
            entry = area_entry(cl)
            key = f"{entry['id']}|{entry.get('btnFrom', '')}|{entry.get('btnTo', '')}"
            if key not in closed_area_seen:
                closed_area_seen.add(key)
                closed_area_list.append(entry)

    return sorted(rwy_set), closed_area_list, restriction_area_list, skipped


def main() -> int:
    parser = argparse.ArgumentParser(description="Scrape and filter active NOTAM closures.")
    parser.add_argument("icao", help="ICAO code, e.g. KJFK")
    parser.add_argument(
        "--output", "-o", required=True,
        help="Output JSON path, or '-' for stdout.",
    )
    args = parser.parse_args()

    raw = fetch_raw_notams(args.icao)
    if not raw:
        print("[notams] no NOTAMs scraped — aborting", file=sys.stderr)
        return 1

    now = datetime.now(timezone.utc)
    rwy, closed_areas, restriction_areas, skipped = filter_closures(raw, now)
    print(
        f"[notams] {args.icao.upper()}: {len(rwy)} runway closure(s), "
        f"{len(closed_areas)} closed area(s), "
        f"{len(restriction_areas)} restriction area(s), {len(skipped)} skipped",
        file=sys.stderr,
    )
    for body in skipped:
        print(f"[notams]   skipped: {body}", file=sys.stderr)

    payload = {
        "icao": args.icao.upper(),
        "fetchedAt": now.isoformat().replace("+00:00", "Z"),
        "rwyClosures": rwy,
        "closedAreas": closed_areas,
        "restrictionAreas": restriction_areas,
    }

    text = json.dumps(payload, indent=2)
    if args.output == "-":
        print(text)
    else:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"[notams] wrote {args.output}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
