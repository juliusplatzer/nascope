#!/usr/bin/env python3
"""Fetch D-ATIS runway data from atis.info for one airport.

The API normally exposes parsed `ldg_rwys` and `dep_rwys` fields. When those
are missing, this script falls back to conservative text parsing for common
D-ATIS runway phrases.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.request
from typing import Any


TEXT_KEYS = {"datis", "raw", "text", "message", "atis", "transcript"}
LANDING_KEYS = {"ldg_rwys", "landing_runways", "landingrunways"}
DEPARTURE_KEYS = {"dep_rwys", "departure_runways", "departurerunways"}


def append_unique(out: list[str], value: str) -> None:
    value = value.strip().upper()
    if value and value not in out:
        out.append(value)


def normalize_runway(number: str, suffix: str = "") -> str | None:
    try:
        runway = int(number)
    except ValueError:
        return None
    if runway < 1 or runway > 36:
        return None
    return f"{runway}{suffix[:1].upper()}"


def normalize_text(text: str) -> str:
    replacements = {
        r"\bRUNWAYS\b": "RWYS",
        r"\bRUNWAY\b": "RWY",
        r"\bRY\b": "RWY",
        r"\bDEPARTING\b": "DEPG",
        r"\bDEPARTURES\b": "DEPS",
        r"\bDEPARTURE\b": "DEP",
        r"\bDEPTG\b": "DEPG",
        r"\bLNDG\b": "LDG",
        r"\bLANDING\b": "LDG",
        r"\bAPPROACHES\b": "APCHS",
        r"\bAPPROACH\b": "APCH",
    }
    text = text.upper()
    for pattern, replacement in replacements.items():
        text = re.sub(pattern, replacement, text)
    return text


def expand_spoken_runways(text: str) -> str:
    def side(value: str) -> str:
        return {"LEFT": "L", "RIGHT": "R"}.get(value, "C")

    text = re.sub(
        r"\b([0-3])\s+([0-9])\s+(LEFT|RIGHT|CENTER|CENTRE)\b",
        lambda m: f"{m.group(1)}{m.group(2)}{side(m.group(3))}",
        text,
    )
    text = re.sub(
        r"\b([0-3]?\d)\s+(LEFT|RIGHT|CENTER|CENTRE)\b",
        lambda m: f"{m.group(1)}{side(m.group(2))}",
        text,
    )
    return text


def extract_runways(text: str) -> list[str]:
    text = expand_spoken_runways(normalize_text(text))
    out: list[str] = []
    for match in re.finditer(r"(?<![A-Z0-9.])(?:RWYS?\s*)?([0-3]?\d)\s*([LRC])?(?![A-Z0-9.])", text):
        runway = normalize_runway(match.group(1), match.group(2) or "")
        if runway:
            append_unique(out, runway)
    return out


def has_any(text: str, needles: tuple[str, ...]) -> bool:
    return any(needle in text for needle in needles)


def parse_text_runways(raw: str) -> tuple[list[str], list[str]]:
    landing: list[str] = []
    departure: list[str] = []

    for sentence in re.split(r"[.;]", normalize_text(raw)):
        sentence = sentence.strip()
        if not sentence:
            continue

        out_of_service = has_any(sentence, (" CLSD", " CLOSED", " OTS", " OUT OF SERVICE"))
        operational = has_any(sentence, (" IN USE", " EXPECT", " LDG", " DEPG", " DEP "))
        if out_of_service and not operational:
            continue

        both = "LDG AND DEPG" in sentence or "LND AND DEPG" in sentence
        has_dep = has_any(sentence, ("DEPG", "DEP ", "DEPS"))
        has_ldg = has_any(sentence, ("LDG", "ARRIVAL", "ARRIVALS", "APCH", "ILS", "VISUAL"))
        generic_in_use = sentence.startswith("RWYS") and " IN USE" in sentence and not has_dep

        if both or generic_in_use:
            rwys = extract_runways(sentence)
            for rwy in rwys:
                append_unique(landing, rwy)
                append_unique(departure, rwy)
            continue

        if has_dep and has_ldg:
            candidates = [idx for token in ("DEPG", "DEPS", "DEP ") if (idx := sentence.find(token)) >= 0]
            split_at = min(candidates) if candidates else len(sentence)
            for rwy in extract_runways(sentence[:split_at]):
                append_unique(landing, rwy)
            for rwy in extract_runways(sentence[split_at:]):
                append_unique(departure, rwy)
            continue

        if has_dep:
            for rwy in extract_runways(sentence):
                append_unique(departure, rwy)
        elif has_ldg:
            for rwy in extract_runways(sentence):
                append_unique(landing, rwy)

    return landing, departure


def runways_from_value(value: Any) -> list[str]:
    out: list[str] = []
    if isinstance(value, list):
        for item in value:
            for rwy in runways_from_value(item):
                append_unique(out, rwy)
    elif isinstance(value, (str, int, float)):
        for rwy in extract_runways(str(value)):
            append_unique(out, rwy)
    return out


def collect(data: Any) -> tuple[list[str], list[str], list[str]]:
    landing: list[str] = []
    departure: list[str] = []
    texts: list[str] = []

    def walk(value: Any) -> None:
        if isinstance(value, list):
            for item in value:
                walk(item)
            return
        if not isinstance(value, dict):
            return
        for key, item in value.items():
            normalized_key = key.lower()
            if normalized_key in LANDING_KEYS:
                for rwy in runways_from_value(item):
                    append_unique(landing, rwy)
            elif normalized_key in DEPARTURE_KEYS:
                for rwy in runways_from_value(item):
                    append_unique(departure, rwy)
            elif normalized_key in TEXT_KEYS and isinstance(item, str):
                texts.append(item)
            else:
                walk(item)

    walk(data)

    if not landing or not departure:
        for text in texts:
            parsed_landing, parsed_departure = parse_text_runways(text)
            for rwy in parsed_landing:
                append_unique(landing, rwy)
            for rwy in parsed_departure:
                append_unique(departure, rwy)

    return landing, departure, texts


def fetch(icao: str) -> Any:
    template = os.environ.get("NASCOPE_ATIS_URL_TEMPLATE", "https://atis.info/api/{icao}")
    icao = icao.upper()
    url = template.replace("%1", icao).format(icao=icao, ICAO=icao)
    req = urllib.request.Request(url, headers={"Accept": "application/json", "User-Agent": "nascope/1.0"})
    with urllib.request.urlopen(req, timeout=10) as response:
        return json.loads(response.read().decode("utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("icao", help="ICAO airport id, e.g. KSFO")
    args = parser.parse_args()

    landing, departure, _ = collect(fetch(args.icao))
    print(json.dumps({"icao": args.icao.upper(), "ldg_rwys": landing, "dep_rwys": departure}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
