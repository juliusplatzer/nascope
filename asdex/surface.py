#!/usr/bin/env python3
"""
Fetch FAA Airport Mapping runway/taxiway polygons for one airport and emit JSON.

Default heading source:
  1. AirNav airport page runway headings (magnetic/true from FAA-record style page)
  2. BTS/NTAD TRUE_ALIGNMENT + airport magnetic variation fallback
  3. "NA" by default

Output:
{
  "icao": "KJFK",
  "rwys": [
    {"id": "04L/22R", "track": "044/224", "polygon": [[lon, lat], ...]}
  ],
  "twys": [
    {"id": "A", "polygon": [[lon, lat], ...]}
  ],
  "hbs": [
    {"id": "04L/22R:A:17", "runway": "04L/22R", "polygon": [[lon, lat], [lon, lat]]}
  ],
  "gates": [
    {"id": "A12", "position": [lon, lat]}
  ]
}

Notes:
  - Coordinates are [longitude, latitude], WGS84/EPSG:4326.
  - Runway polygons are reduced to four outer corner points.
  - Taxiway polygons remain full exterior rings.
  - Holdbars are derived from taxiway polygon edges near runways; each holdbar
    polygon contains exactly two [longitude, latitude] points.
  - Gates are fetched from OpenStreetMap/Overpass as aeroway=gate
    elements near the airport's aeroway=aerodrome object.
  - AirNav is HTML, not an official API; it is used because it exposes the magnetic
    runway heading directly in simple text. For official-only data, use --heading-source bts.
  - Only Python standard library is required.
"""

from __future__ import annotations

import argparse
import html
import json
import math
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import defaultdict
from typing import Any, Callable


ARCGIS_SEARCH_URL = "https://www.arcgis.com/sharing/rest/search"
AIRNAV_URL_TEMPLATE = "https://www.airnav.com/airport/{code}"
OVERPASS_INTERPRETER_URL = "https://overpass-api.de/api/interpreter"
DEFAULT_USER_AGENT = "faa-am-airport-surfaces/2.1 (contact: set --user-agent to your email or project URL)"
USER_AGENT = DEFAULT_USER_AGENT

# BTS/NTAD Runway Ends table. Table id 1 contains RWY_ID, RWY_END_ID, TRUE_ALIGNMENT.
BTS_RUNWAY_ENDS_TABLE_URL = (
    "https://services.arcgis.com/xOi1kZaI0eWDREZv/ArcGIS/rest/services/"
    "Runway_Ends_Table/FeatureServer/1"
)

# BTS/NTAD Aviation Facilities layer. Contains airport magnetic-variation fields.
BTS_AVIATION_FACILITIES_URL = (
    "https://services.arcgis.com/xOi1kZaI0eWDREZv/ArcGIS/rest/services/"
    "NTAD_Aviation_Facilities/FeatureServer/0"
)

FALLBACK_SERVICES = {
    "AM_Runway": [
        "https://services6.arcgis.com/ssFJjBXIUyZDrSYZ/ArcGIS/rest/services/AM_Runway/FeatureServer",
    ],
    "AM_Taxiway": [
        "https://services6.arcgis.com/ssFJjBXIUyZDrSYZ/ArcGIS/rest/services/AM_Taxiway/FeatureServer",
    ],
}


class SurfaceFetchError(RuntimeError):
    """Raised for expected fetch/parse failures."""


# ----------------------------- HTTP helpers -----------------------------


def make_request(url: str) -> urllib.request.Request:
    return urllib.request.Request(
        url,
        headers={
            "User-Agent": USER_AGENT,
            "Accept": "text/html,application/json;q=0.9,*/*;q=0.8",
        },
    )


def add_query(url: str, params: dict[str, Any] | None) -> str:
    if not params:
        return url
    return url + "?" + urllib.parse.urlencode(params)


def http_json(url: str, params: dict[str, Any] | None = None, timeout: float = 30.0) -> dict[str, Any]:
    """GET JSON. Keeps memory usage low by only requesting the airport/features needed."""
    full_url = add_query(url, params)
    try:
        with urllib.request.urlopen(make_request(full_url), timeout=timeout) as resp:
            raw = resp.read()
    except Exception as exc:
        raise SurfaceFetchError(f"HTTP request failed for {url}: {exc}") from exc

    try:
        data = json.loads(raw.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise SurfaceFetchError(f"Non-JSON response from {url}") from exc

    if isinstance(data, dict) and "error" in data:
        err = data["error"]
        if isinstance(err, dict):
            raise SurfaceFetchError(f"{err.get('message', 'ArcGIS error')}: {err.get('details', [])}")
        raise SurfaceFetchError(str(err))

    if not isinstance(data, dict):
        raise SurfaceFetchError(f"Unexpected JSON response type from {url}: {type(data).__name__}")

    return data


def http_text(url: str, params: dict[str, Any] | None = None, timeout: float = 30.0) -> str:
    """GET text/HTML."""
    full_url = add_query(url, params)
    try:
        with urllib.request.urlopen(make_request(full_url), timeout=timeout) as resp:
            raw = resp.read()
    except Exception as exc:
        raise SurfaceFetchError(f"HTTP request failed for {url}: {exc}") from exc
    return raw.decode("utf-8", errors="replace")


# ----------------------------- OSM Overpass gates -----------------------------


def overpass_string(value: str) -> str:
    """Escape a Python string for a double-quoted Overpass QL string literal."""
    return value.replace("\\", "\\\\").replace('"', '\\"')


def build_overpass_gates_query(
    icao: str,
    overpass_timeout: int = 60,
    around_m: float = 8000.0,
) -> str:
    """
    Build an Overpass Turbo-compatible query for airport gates.

    This intentionally uses the robust pattern that works in Overpass Turbo:

        nwr["aeroway"="aerodrome"]["icao"="KATL"]->.apt;
        node/way/relation(around.apt:8000)["aeroway"="gate"];
        out center;

    The `around.apt` selector avoids the map_to_area conversion that some
    Overpass instances reject with HTTP 406.
    """
    qicao = overpass_string(icao.strip().upper())
    timeout_s = max(1, int(round(overpass_timeout)))
    radius_m = max(1.0, float(around_m))
    radius_text = str(int(radius_m)) if radius_m.is_integer() else f"{radius_m:.3f}".rstrip("0").rstrip(".")
    return f'''
[out:json][timeout:{timeout_s}];

nwr["aeroway"="aerodrome"]["icao"="{qicao}"]->.apt;

(
  node(around.apt:{radius_text})["aeroway"="gate"];
  way(around.apt:{radius_text})["aeroway"="gate"];
  relation(around.apt:{radius_text})["aeroway"="gate"];
);

out center;
'''.strip()


def build_overpass_gates_bbox_query(
    south: float,
    west: float,
    north: float,
    east: float,
    overpass_timeout: int = 25,
) -> str:
    """
    Build a robust Overpass query for gates inside a bbox.

    Overpass area lookup can fail for some aerodrome tagging combinations or
    server versions. Since this script already fetched FAA airport geometry, a
    tight airport bbox is simpler and more reliable. Overpass bbox order is:

        south, west, north, east
    """
    timeout_s = max(1, int(round(overpass_timeout)))
    bbox = f"{south:.8f},{west:.8f},{north:.8f},{east:.8f}"
    return "\n".join(
        [
            f"[out:json][timeout:{timeout_s}];",
            "(",
            f"  node[\"aeroway\"=\"gate\"]({bbox});",
            f"  way[\"aeroway\"=\"gate\"]({bbox});",
            f"  relation[\"aeroway\"=\"gate\"]({bbox});",
            ");",
            "out body geom;",
        ]
    )


def airport_bbox_from_rings(
    rings: list[list[list[float]]],
    padding_m: float = 300.0,
) -> tuple[float, float, float, float]:
    """
    Return a padded airport bbox as (south, west, north, east).

    Input rings use [lon, lat]. Padding is approximate but sufficient for
    airport-scale Overpass bbox queries.
    """
    points: list[list[float]] = []
    for ring in rings:
        points.extend(open_ring(ring))

    if not points:
        raise ValueError("Need at least one coordinate to compute airport bbox")

    min_lon = min(float(p[0]) for p in points)
    max_lon = max(float(p[0]) for p in points)
    min_lat = min(float(p[1]) for p in points)
    max_lat = max(float(p[1]) for p in points)

    mid_lat = 0.5 * (min_lat + max_lat)
    earth_r = 6371008.8
    pad_lat = math.degrees(float(padding_m) / earth_r)
    cos_lat = max(math.cos(math.radians(mid_lat)), 1e-12)
    pad_lon = math.degrees(float(padding_m) / (earth_r * cos_lat))

    south = max(-90.0, min_lat - pad_lat)
    north = min(90.0, max_lat + pad_lat)
    west = max(-180.0, min_lon - pad_lon)
    east = min(180.0, max_lon + pad_lon)
    return south, west, north, east


def http_overpass_json(
    query: str,
    url: str,
    timeout: float,
    user_agent: str | None = None,
    max_retries: int = 4,
) -> dict[str, Any]:
    """POST an Overpass QL query and parse the JSON response.

    overpass-api.de rejects generic browser-like User-Agent strings such as
    Mozilla/5.0. Use a custom identifying User-Agent and back off on 429.
    """

    ua = (user_agent or USER_AGENT).strip()
    if not ua or ua.lower().startswith("mozilla"):
        raise SurfaceFetchError(
            "Overpass User-Agent must be a custom identifying string and must not start with Mozilla. "
            "Pass --user-agent 'your-project/1.0 (your@email-or-url)'."
        )

    common_headers = {
        "User-Agent": ua,
        "Accept": "application/json,text/plain;q=0.9,*/*;q=0.8",
    }

    requests = [
        (
            "POST form",
            urllib.request.Request(
                url,
                data=urllib.parse.urlencode({"data": query}).encode("utf-8"),
                headers={
                    **common_headers,
                    "Content-Type": "application/x-www-form-urlencoded; charset=utf-8",
                },
                method="POST",
            ),
        ),
        (
            "POST raw",
            urllib.request.Request(
                url,
                data=query.encode("utf-8"),
                headers={
                    **common_headers,
                    "Content-Type": "text/plain; charset=utf-8",
                },
                method="POST",
            ),
        ),
    ]

    errors: list[str] = []
    for attempt in range(max(1, max_retries)):
        for label, req in requests:
            try:
                with urllib.request.urlopen(req, timeout=timeout) as resp:
                    raw = resp.read()
                break
            except urllib.error.HTTPError as exc:
                body = exc.read().decode("utf-8", errors="replace")[:1000]

                if exc.code == 429 and attempt + 1 < max_retries:
                    retry_after = exc.headers.get("Retry-After")
                    try:
                        delay = float(retry_after) if retry_after else 2.0 ** attempt
                    except ValueError:
                        delay = 2.0 ** attempt
                    delay = min(max(delay, 1.0), 60.0)
                    print(
                        f"warning: Overpass rate-limited request with 429; retrying in {delay:.0f}s",
                        file=sys.stderr,
                    )
                    time.sleep(delay)
                    break

                errors.append(f"{label}: HTTP Error {exc.code}: {exc.reason}; body={body!r}")
            except Exception as exc:
                errors.append(f"{label}: {exc}")
        else:
            # Both request encodings failed with non-retryable errors.
            raise SurfaceFetchError("Overpass request failed: " + " | ".join(errors[-2:]))

        if 'raw' in locals():
            try:
                data = json.loads(raw.decode("utf-8"))
            except json.JSONDecodeError as exc:
                snippet = raw[:500].decode("utf-8", errors="replace")
                raise SurfaceFetchError(f"Overpass returned non-JSON response: {snippet!r}") from exc

            if not isinstance(data, dict):
                raise SurfaceFetchError(f"Unexpected Overpass JSON response type: {type(data).__name__}")
            return data

    raise SurfaceFetchError("Overpass request failed after retries: " + " | ".join(errors[-4:]))


def rounded_lonlat(lon: float, lat: float, digits: int) -> list[float]:
    return [round(float(lon), digits), round(float(lat), digits)]


def gate_position_from_osm_element(element: dict[str, Any]) -> list[float] | None:
    """
    Return representative [lon, lat] for an OSM gate.

    For nodes this is the node coordinate. For ways, OSM convention says the
    direction points toward the nose-wheel stop position, so the last geometry
    vertex is the most useful gate coordinate when geometry is available.
    For relations or missing geometry, fall back to the Overpass center.
    """
    etype = str(element.get("type") or "")

    if etype == "node" and "lon" in element and "lat" in element:
        return [float(element["lon"]), float(element["lat"])]

    geom = element.get("geometry")
    if etype == "way" and isinstance(geom, list) and geom:
        last = geom[-1]
        if isinstance(last, dict) and "lon" in last and "lat" in last:
            return [float(last["lon"]), float(last["lat"])]

    center = element.get("center")
    if isinstance(center, dict) and "lon" in center and "lat" in center:
        return [float(center["lon"]), float(center["lat"])]

    if isinstance(geom, list) and geom:
        lon_sum = 0.0
        lat_sum = 0.0
        count = 0
        for point in geom:
            if isinstance(point, dict) and "lon" in point and "lat" in point:
                lon_sum += float(point["lon"])
                lat_sum += float(point["lat"])
                count += 1
        if count:
            return [lon_sum / count, lat_sum / count]

    return None


def gate_geometry_from_osm_element(element: dict[str, Any], digits: int) -> list[list[float]] | None:
    """Return a rounded lon/lat geometry list for OSM way/relation gates, if present."""
    geom = element.get("geometry")
    if not isinstance(geom, list) or not geom:
        return None

    out: list[list[float]] = []
    for point in geom:
        if isinstance(point, dict) and "lon" in point and "lat" in point:
            out.append(rounded_lonlat(float(point["lon"]), float(point["lat"]), digits))
    return out or None


def osm_gate_to_gate(element: dict[str, Any], digits: int) -> dict[str, Any] | None:
    """Convert one Overpass gate element into the minimal gate schema.

    Output shape:
        {"id": <ref|name|osm:type/id>, "position": [lon, lat]}
    """
    tags = element.get("tags") or {}
    if not isinstance(tags, dict):
        tags = {}

    pos = gate_position_from_osm_element(element)
    if pos is None:
        return None

    etype = str(element.get("type") or "unknown")
    osm_id = element.get("id")
    ref = str(tags.get("ref") or "").strip()
    name = str(tags.get("name") or "").strip()
    gate_id = ref or name or f"osm:{etype}/{osm_id}"

    return {
        "id": gate_id,
        "position": rounded_lonlat(pos[0], pos[1], digits),
    }


def fetch_osm_gates(
    icao: str,
    digits: int,
    overpass_url: str = OVERPASS_INTERPRETER_URL,
    overpass_timeout: float = 30.0,
    bbox: tuple[float, float, float, float] | None = None,
    user_agent: str | None = None,
) -> list[dict[str, Any]]:
    """Fetch OSM gate elements and return them as JSON-ready gates."""
    # Use the nwr aerodrome -> around.apt query. It has proven more reliable
    # across Overpass instances than map_to_area or bbox-based gate lookup.
    # The bbox argument is kept for backward compatibility but intentionally
    # ignored.
    query = build_overpass_gates_query(icao, int(overpass_timeout))
    data = http_overpass_json(query, overpass_url, overpass_timeout, user_agent=user_agent)
    elements = data.get("elements", [])
    if not isinstance(elements, list):
        raise SurfaceFetchError("Overpass JSON did not contain an elements list")

    gates: list[dict[str, Any]] = []
    seen: set[tuple[str, Any]] = set()
    for element in elements:
        if not isinstance(element, dict):
            continue
        key = (str(element.get("type") or ""), element.get("id"))
        if key in seen:
            continue
        seen.add(key)

        gate = osm_gate_to_gate(element, digits)
        if gate is not None:
            gates.append(gate)

    gates.sort(key=lambda x: natural_key(str(x.get("id", ""))))
    return gates


# ----------------------------- ArcGIS FAA AM polygons -----------------------------


def normalize_service_url(url: str) -> str:
    """Return FeatureServer root even if ArcGIS search gives FeatureServer/0."""
    url = url.split("?", 1)[0].rstrip("/")
    if url.rsplit("/", 1)[-1].isdigit():
        url = url.rsplit("/", 1)[0]
    return url


def layer0_url(service_url: str) -> str:
    return normalize_service_url(service_url) + "/0"


def score_arcgis_item(item: dict[str, Any], service_name: str, pending: bool) -> int:
    title = str(item.get("title", ""))
    url = str(item.get("url", ""))
    blob = json.dumps(item, sort_keys=True)

    def compact(s: str) -> str:
        return s.lower().replace("_", "").replace(" ", "")

    score = 0
    if compact(title) == compact(service_name):
        score += 100
    if f"/{service_name}/FeatureServer" in url:
        score += 100
    if "Federal Aviation Administration" in blob:
        score += 20
    if "AIS" in blob or "Aeronautical" in blob:
        score += 10
    if "pending" in title.lower() and not pending:
        score -= 1000
    return score


def discover_service_candidates(service_name: str, pending: bool, timeout: float) -> list[str]:
    queries = [
        f'title:"{service_name}" AND type:"Feature Service"',
        f'title:"{service_name.replace("_", " ")}" AND type:"Feature Service"',
        f'"{service_name}" AND type:"Feature Service"',
    ]

    scored: list[tuple[int, str]] = []
    for q in queries:
        try:
            data = http_json(ARCGIS_SEARCH_URL, {"f": "json", "q": q, "num": 20}, timeout)
        except Exception:
            continue

        for item in data.get("results", []):
            url = item.get("url")
            if not url or "FeatureServer" not in str(url):
                continue
            scored.append((score_arcgis_item(item, service_name, pending), normalize_service_url(str(url))))

    scored.sort(reverse=True)

    out: list[str] = []
    seen: set[str] = set()
    for _, url in scored:
        if url not in seen:
            seen.add(url)
            out.append(url)

    for url in FALLBACK_SERVICES.get(service_name, []):
        normalized = normalize_service_url(url)
        if normalized not in seen:
            seen.add(normalized)
            out.append(normalized)

    return out


def choose_service_url(service_name: str, override_url: str | None, pending: bool, timeout: float) -> str:
    if override_url:
        return normalize_service_url(override_url)

    errors: list[str] = []
    for service_url in discover_service_candidates(service_name, pending, timeout):
        try:
            meta = http_json(layer0_url(service_url), {"f": "json"}, timeout)
            if meta.get("geometryType") == "esriGeometryPolygon":
                return service_url
            errors.append(f"{service_url}: geometryType={meta.get('geometryType')!r}")
        except Exception as exc:
            errors.append(f"{service_url}: {exc}")

    details = "\n  ".join(errors) if errors else "no candidates found"
    raise SurfaceFetchError(f"Could not find usable {service_name} FeatureServer. Tried:\n  {details}")


def sql_quote(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def arcgis_polygon_to_geojson(geometry: dict[str, Any] | None) -> dict[str, Any] | None:
    if not geometry:
        return None
    if "rings" in geometry:
        return {"type": "Polygon", "coordinates": geometry["rings"]}
    if "curveRings" in geometry:
        raise SurfaceFetchError("ArcGIS returned curveRings; retry with GeoJSON or returnTrueCurves=false")
    return None


def query_features(
    service_url: str,
    where: str,
    out_fields: list[str],
    timeout: float,
    page_size: int = 2000,
) -> list[dict[str, Any]]:
    """Query FeatureServer/0 with pagination. Prefer GeoJSON coordinate arrays."""
    query_url = layer0_url(service_url) + "/query"
    features: list[dict[str, Any]] = []
    offset = 0

    while True:
        params = {
            "f": "geojson",
            "where": where,
            "outFields": ",".join(out_fields),
            "returnGeometry": "true",
            "outSR": "4326",
            "orderByFields": "OBJECTID ASC",
            "resultRecordCount": str(page_size),
            "resultOffset": str(offset),
        }

        try:
            data = http_json(query_url, params, timeout)
            batch = data.get("features", [])
            features.extend(batch)
            exceeded = bool(data.get("exceededTransferLimit"))
        except Exception:
            params["f"] = "json"
            params["returnTrueCurves"] = "false"
            data = http_json(query_url, params, timeout)
            raw_batch = data.get("features", [])
            batch = [
                {
                    "type": "Feature",
                    "properties": feat.get("attributes", {}),
                    "geometry": arcgis_polygon_to_geojson(feat.get("geometry")),
                }
                for feat in raw_batch
            ]
            features.extend(batch)
            exceeded = bool(data.get("exceededTransferLimit"))

        if not batch:
            break
        if not exceeded and len(batch) < page_size:
            break
        offset += len(batch)

    return features


def query_table_rows(
    table_url: str,
    where: str,
    out_fields: list[str],
    timeout: float,
    page_size: int = 2000,
) -> list[dict[str, Any]]:
    query_url = table_url.rstrip("/") + "/query"
    rows: list[dict[str, Any]] = []
    offset = 0

    while True:
        data = http_json(
            query_url,
            {
                "f": "json",
                "where": where,
                "outFields": ",".join(out_fields),
                "returnGeometry": "false",
                "orderByFields": "OBJECTID ASC",
                "resultRecordCount": str(page_size),
                "resultOffset": str(offset),
            },
            timeout,
        )
        batch = data.get("features", [])
        rows.extend((feat.get("attributes") or {}) for feat in batch)

        if not batch:
            break
        if not data.get("exceededTransferLimit") and len(batch) < page_size:
            break
        offset += len(batch)

    return rows


# ----------------------------- geometry + runway id helpers -----------------------------


def open_ring(ring: list[Any]) -> list[list[float]]:
    """Drop invalid points, duplicate consecutive points, and duplicate closing point."""
    pts: list[list[float]] = []
    for p in ring:
        if not isinstance(p, (list, tuple)) or len(p) < 2:
            continue
        pt = [float(p[0]), float(p[1])]
        if not pts or pt != pts[-1]:
            pts.append(pt)

    if len(pts) > 1:
        a, b = pts[0], pts[-1]
        if abs(a[0] - b[0]) < 1e-12 and abs(a[1] - b[1]) < 1e-12:
            pts.pop()
    return pts


def exterior_rings(geometry: dict[str, Any] | None) -> list[list[list[float]]]:
    """Return exterior rings only. Holes are ignored for this output format."""
    if not geometry:
        return []

    gtype = geometry.get("type")
    coords = geometry.get("coordinates") or []
    rings: list[list[list[float]]] = []

    if gtype == "Polygon" and coords:
        rings.append(open_ring(coords[0]))
    elif gtype == "MultiPolygon":
        for polygon in coords:
            if polygon:
                rings.append(open_ring(polygon[0]))

    return [r for r in rings if len(r) >= 3]


def round_ring(ring: list[list[float]], digits: int) -> list[list[float]]:
    return [[round(float(p[0]), digits), round(float(p[1]), digits)] for p in ring]


def local_lonlat_projector(
    ring: list[list[float]],
) -> tuple[
    list[tuple[float, float]],
    Callable[[float, float], list[float]],
]:
    """
    Project a small lon/lat polygon to a local metric plane.

    This keeps the script dependency-free. For airport/runway-sized polygons,
    the local equirectangular projection is accurate enough for computing
    bearings, convex hulls, and minimum-area rectangles.

    Returns:
        - projected points in meters,
        - inverse function mapping (x, y) meters back to [lon, lat].
    """
    pts = open_ring(ring)
    if len(pts) < 3:
        raise ValueError("Need at least three runway polygon points")

    lon0 = sum(p[0] for p in pts) / len(pts)
    lat0 = sum(p[1] for p in pts) / len(pts)
    cos_lat0 = max(math.cos(math.radians(lat0)), 1e-12)
    earth_r = 6371008.8

    projected: list[tuple[float, float]] = []
    for lon, lat in pts:
        x = earth_r * math.radians(lon - lon0) * cos_lat0
        y = earth_r * math.radians(lat - lat0)
        projected.append((x, y))

    def inverse(x: float, y: float) -> list[float]:
        lon = lon0 + math.degrees(x / (earth_r * cos_lat0))
        lat = lat0 + math.degrees(y / earth_r)
        return [lon, lat]

    return projected, inverse


def convex_hull_xy(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    """
    Return the convex hull of 2D points using Andrew's monotone chain algorithm.

    The returned hull is open, ordered around the boundary, and contains no
    repeated final point.
    """
    unique = sorted(set(points))
    if len(unique) <= 1:
        return unique

    def cross(
        o: tuple[float, float],
        a: tuple[float, float],
        b: tuple[float, float],
    ) -> float:
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower: list[tuple[float, float]] = []
    for p in unique:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0.0:
            lower.pop()
        lower.append(p)

    upper: list[tuple[float, float]] = []
    for p in reversed(unique):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0.0:
            upper.pop()
        upper.append(p)

    return lower[:-1] + upper[:-1]


def minimum_area_rectangle_xy(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    """
    Return the four corners of the minimum-area rotated rectangle.

    Input should normally be a convex hull. The implementation checks each hull
    edge orientation. This is simple and reliable for small AM runway hulls.
    """
    hull = convex_hull_xy(points)
    if len(hull) < 3:
        raise ValueError("Need at least three non-collinear runway polygon points")

    best_area = float("inf")
    best_rect: list[tuple[float, float]] | None = None
    n = len(hull)

    for i in range(n):
        x1, y1 = hull[i]
        x2, y2 = hull[(i + 1) % n]
        dx = x2 - x1
        dy = y2 - y1
        edge_len = math.hypot(dx, dy)
        if edge_len == 0.0:
            continue

        ux = dx / edge_len
        uy = dy / edge_len

        # v is perpendicular to u. Projecting onto (u, v) is a rotation.
        vx = -uy
        vy = ux

        min_u = max_u = hull[0][0] * ux + hull[0][1] * uy
        min_v = max_v = hull[0][0] * vx + hull[0][1] * vy

        for x, y in hull[1:]:
            pu = x * ux + y * uy
            pv = x * vx + y * vy
            if pu < min_u:
                min_u = pu
            elif pu > max_u:
                max_u = pu
            if pv < min_v:
                min_v = pv
            elif pv > max_v:
                max_v = pv

        area = (max_u - min_u) * (max_v - min_v)
        if area < best_area:
            best_area = area
            # Convert rectangle corners back from projected axis coordinates.
            best_rect = [
                (min_u * ux + min_v * vx, min_u * uy + min_v * vy),
                (max_u * ux + min_v * vx, max_u * uy + min_v * vy),
                (max_u * ux + max_v * vx, max_u * uy + max_v * vy),
                (min_u * ux + max_v * vx, min_u * uy + max_v * vy),
            ]

    if best_rect is None:
        raise ValueError("Could not compute runway corner rectangle")

    return best_rect


def runway_outer_corner_polygon(ring: list[list[float]]) -> list[list[float]]:
    """
    Replace an AM_Runway boundary ring with its four outer rectangle corners.

    Pipeline:
        lon/lat ring -> local meter projection -> convex hull
        -> minimum-area rotated rectangle -> lon/lat four-corner polygon

    The returned ring is open: it contains exactly four [lon, lat] points and
    does not repeat the first point at the end, matching this script's existing
    JSON polygon convention.
    """
    projected, inverse = local_lonlat_projector(ring)
    rect_xy = minimum_area_rectangle_xy(projected)
    return [inverse(x, y) for x, y in rect_xy]


def make_local_projector(
    rings: list[list[list[float]]],
) -> Callable[[float, float], tuple[float, float]]:
    """Return a lon/lat -> local meter projector for a small airport area."""
    pts: list[list[float]] = []
    for ring in rings:
        pts.extend(open_ring(ring))

    if not pts:
        raise ValueError("Need at least one coordinate to build a local projector")

    lon0 = sum(p[0] for p in pts) / len(pts)
    lat0 = sum(p[1] for p in pts) / len(pts)
    cos_lat0 = max(math.cos(math.radians(lat0)), 1e-12)
    earth_r = 6371008.8

    def project(lon: float, lat: float) -> tuple[float, float]:
        x = earth_r * math.radians(lon - lon0) * cos_lat0
        y = earth_r * math.radians(lat - lat0)
        return x, y

    return project


def ring_edges_xy(
    ring_xy: list[tuple[float, float]],
) -> list[tuple[int, tuple[float, float], tuple[float, float]]]:
    """Return indexed edges of an open polygon ring in projected coordinates."""
    n = len(ring_xy)
    if n < 2:
        return []
    return [(i, ring_xy[i], ring_xy[(i + 1) % n]) for i in range(n)]


def rectangle_long_axis_angle_xy(rect_xy: list[tuple[float, float]]) -> float:
    """Return the undirected angle, in radians, of a rectangle's long side."""
    if len(rect_xy) != 4:
        raise ValueError("Expected a four-corner rectangle")

    best_len = -1.0
    best_angle = 0.0
    for _, a, b in ring_edges_xy(rect_xy):
        dx = b[0] - a[0]
        dy = b[1] - a[1]
        length = math.hypot(dx, dy)
        if length > best_len:
            best_len = length
            best_angle = math.atan2(dy, dx)
    return best_angle


def undirected_angle_diff_rad(a: float, b: float) -> float:
    """Smallest angle between two undirected lines, in radians."""
    return abs((a - b + math.pi / 2.0) % math.pi - math.pi / 2.0)


def point_segment_distance_xy(
    p: tuple[float, float],
    a: tuple[float, float],
    b: tuple[float, float],
) -> float:
    """Euclidean distance from point p to segment ab."""
    ax, ay = a
    bx, by = b
    px, py = p
    dx = bx - ax
    dy = by - ay
    denom = dx * dx + dy * dy
    if denom == 0.0:
        return math.hypot(px - ax, py - ay)

    t = ((px - ax) * dx + (py - ay) * dy) / denom
    if t <= 0.0:
        qx, qy = ax, ay
    elif t >= 1.0:
        qx, qy = bx, by
    else:
        qx = ax + t * dx
        qy = ay + t * dy
    return math.hypot(px - qx, py - qy)


def _orientation(
    a: tuple[float, float],
    b: tuple[float, float],
    c: tuple[float, float],
) -> float:
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def _on_segment(
    a: tuple[float, float],
    b: tuple[float, float],
    p: tuple[float, float],
    eps: float = 1e-9,
) -> bool:
    return (
        min(a[0], b[0]) - eps <= p[0] <= max(a[0], b[0]) + eps
        and min(a[1], b[1]) - eps <= p[1] <= max(a[1], b[1]) + eps
        and abs(_orientation(a, b, p)) <= eps
    )


def segments_intersect_xy(
    a: tuple[float, float],
    b: tuple[float, float],
    c: tuple[float, float],
    d: tuple[float, float],
    eps: float = 1e-9,
) -> bool:
    """Return True if segments ab and cd intersect or touch."""
    o1 = _orientation(a, b, c)
    o2 = _orientation(a, b, d)
    o3 = _orientation(c, d, a)
    o4 = _orientation(c, d, b)

    if (o1 > eps and o2 < -eps or o1 < -eps and o2 > eps) and (
        o3 > eps and o4 < -eps or o3 < -eps and o4 > eps
    ):
        return True

    return (
        _on_segment(a, b, c, eps)
        or _on_segment(a, b, d, eps)
        or _on_segment(c, d, a, eps)
        or _on_segment(c, d, b, eps)
    )


def segment_segment_distance_xy(
    a: tuple[float, float],
    b: tuple[float, float],
    c: tuple[float, float],
    d: tuple[float, float],
) -> float:
    """Euclidean distance between two segments."""
    if segments_intersect_xy(a, b, c, d):
        return 0.0
    return min(
        point_segment_distance_xy(a, c, d),
        point_segment_distance_xy(b, c, d),
        point_segment_distance_xy(c, a, b),
        point_segment_distance_xy(d, a, b),
    )


def point_in_polygon_xy(
    p: tuple[float, float],
    polygon_xy: list[tuple[float, float]],
) -> bool:
    """Ray-casting point-in-polygon test for an open exterior ring."""
    x, y = p
    inside = False
    n = len(polygon_xy)
    if n < 3:
        return False

    j = n - 1
    for i in range(n):
        xi, yi = polygon_xy[i]
        xj, yj = polygon_xy[j]

        if _on_segment((xj, yj), (xi, yi), p):
            return True

        crosses = (yi > y) != (yj > y)
        if crosses:
            x_at_y = (xj - xi) * (y - yi) / (yj - yi) + xi
            if x < x_at_y:
                inside = not inside
        j = i

    return inside


def segment_polygon_distance_xy(
    a: tuple[float, float],
    b: tuple[float, float],
    polygon_xy: list[tuple[float, float]],
) -> float:
    """Shortest distance from segment ab to a polygon exterior ring."""
    if len(polygon_xy) < 3:
        return float("inf")

    if point_in_polygon_xy(a, polygon_xy) or point_in_polygon_xy(b, polygon_xy):
        return 0.0

    best = float("inf")
    for _, c, d in ring_edges_xy(polygon_xy):
        dist = segment_segment_distance_xy(a, b, c, d)
        if dist == 0.0:
            return 0.0
        if dist < best:
            best = dist
    return best


def safe_id_part(value: str) -> str:
    """Make a stable compact id part without changing the displayed runway id."""
    s = re.sub(r"[^A-Za-z0-9._-]+", "-", value.strip())
    return s.strip("-") or "unknown"


def derive_holdbars(
    runway_entries: list[dict[str, Any]],
    taxiway_entries: list[dict[str, Any]],
    digits: int,
    max_distance_m: float = 10.0,
    max_angle_deg: float = 15.0,
) -> list[dict[str, Any]]:
    """
    Derive approximate holdbars from taxiway polygon edges.

    For each runway and taxiway polygon, this selects the taxiway edge that is
    closest to the runway among edges approximately parallel to the runway long
    side. The selected edge is retained only if it lies within max_distance_m of
    its owning runway and also within max_distance_m of at least one runway.
    """
    if not runway_entries or not taxiway_entries:
        return []

    all_rings = [entry["ring"] for entry in runway_entries] + [entry["ring"] for entry in taxiway_entries]
    project = make_local_projector(all_rings)

    runway_contexts: list[dict[str, Any]] = []
    for entry in runway_entries:
        ring = open_ring(entry["ring"])
        runway_xy = [project(p[0], p[1]) for p in ring]
        if len(runway_xy) < 3:
            continue
        try:
            rect_xy = minimum_area_rectangle_xy(runway_xy)
            long_axis_angle = rectangle_long_axis_angle_xy(rect_xy)
        except Exception:
            continue
        runway_contexts.append(
            {
                "id": entry["id"],
                "ring_xy": runway_xy,
                "angle": long_axis_angle,
            }
        )

    taxiway_contexts: list[dict[str, Any]] = []
    for entry in taxiway_entries:
        ring = open_ring(entry["ring"])
        ring_xy = [project(p[0], p[1]) for p in ring]
        if len(ring_xy) < 3:
            continue
        taxiway_contexts.append(
            {
                "id": entry["id"],
                "ring": ring,
                "ring_xy": ring_xy,
            }
        )

    max_angle_rad = math.radians(max_angle_deg)
    holdbars: list[dict[str, Any]] = []
    seen: set[tuple[str, str, tuple[float, float], tuple[float, float]]] = set()

    for rwy in runway_contexts:
        runway_id = str(rwy["id"])
        runway_xy = rwy["ring_xy"]
        runway_angle = float(rwy["angle"])

        for twy in taxiway_contexts:
            best: tuple[float, float, int, list[float], list[float]] | None = None
            ring = twy["ring"]
            ring_xy = twy["ring_xy"]

            for edge_index, a_xy, b_xy in ring_edges_xy(ring_xy):
                dx = b_xy[0] - a_xy[0]
                dy = b_xy[1] - a_xy[1]
                edge_len = math.hypot(dx, dy)
                if edge_len == 0.0:
                    continue

                edge_angle = math.atan2(dy, dx)
                angle_diff_rad = undirected_angle_diff_rad(edge_angle, runway_angle)
                if angle_diff_rad > max_angle_rad:
                    continue

                distance_to_owner = segment_polygon_distance_xy(a_xy, b_xy, runway_xy)
                a_lonlat = ring[edge_index]
                b_lonlat = ring[(edge_index + 1) % len(ring)]

                # Primary selection: closest edge to this runway.
                # Tie-breakers: better heading alignment, then longer edge.
                candidate = (distance_to_owner, angle_diff_rad, -edge_len, edge_index, a_lonlat, b_lonlat)
                if best is None or candidate < best:
                    best = candidate

            if best is None:
                continue

            distance_to_owner, angle_diff_rad, _neg_len, edge_index, a_lonlat, b_lonlat = best
            if distance_to_owner > max_distance_m:
                continue

            a_xy = project(a_lonlat[0], a_lonlat[1])
            b_xy = project(b_lonlat[0], b_lonlat[1])
            min_any_runway_distance = min(
                segment_polygon_distance_xy(a_xy, b_xy, other["ring_xy"])
                for other in runway_contexts
            )
            if min_any_runway_distance > max_distance_m:
                continue

            rounded_a = (round(float(a_lonlat[0]), digits), round(float(a_lonlat[1]), digits))
            rounded_b = (round(float(b_lonlat[0]), digits), round(float(b_lonlat[1]), digits))
            key = (runway_id, str(twy["id"]), rounded_a, rounded_b)
            reverse_key = (runway_id, str(twy["id"]), rounded_b, rounded_a)
            if key in seen or reverse_key in seen:
                continue
            seen.add(key)

            hb_id = f"{safe_id_part(runway_id)}:{safe_id_part(str(twy['id']))}:{edge_index}"
            holdbars.append(
                {
                    "id": hb_id,
                    "runway": runway_id,
                    "polygon": [list(rounded_a), list(rounded_b)],
                }
            )

    holdbars.sort(
        key=lambda x: (
            natural_key(str(x.get("runway", ""))),
            natural_key(str(x.get("id", ""))),
        )
    )
    return holdbars


def natural_key(value: str) -> list[Any]:
    return [int(x) if x.isdigit() else x.lower() for x in re.split(r"(\d+)", value or "")]


def normalize_runway_end_id(value: Any) -> str:
    """Normalize runway end labels: '04L' -> '4L', 'RW04L' -> '4L'."""
    s = str(value or "").strip().upper()
    if s.startswith("RW"):
        s = s[2:]
    m = re.fullmatch(r"0?([1-9]|[12][0-9]|3[0-6])([LCR]?)", s)
    if not m:
        return s
    return f"{int(m.group(1))}{m.group(2)}"


def runway_pair_order(rwy_id: str) -> list[str]:
    return [normalize_runway_end_id(p) for p in str(rwy_id).split("/") if p.strip()]


def runway_pair_key(rwy_id: str) -> frozenset[str]:
    return frozenset(runway_pair_order(rwy_id))


def format_track_pair(track_a: float, track_b: float) -> str:
    return f"{int(round(track_a)) % 360:03d}/{int(round(track_b)) % 360:03d}"


def infer_faa_id_from_icao(icao: str) -> str | None:
    """Works for many US ICAOs: KJFK -> JFK, PHNL -> HNL, PGUM -> GUM."""
    icao = icao.strip().upper()
    if len(icao) == 4 and icao[0] in {"K", "P"}:
        return icao[1:]
    return None


def airport_where_clause(icao: str, extra_faa_id: str | None) -> str:
    clauses = [f"ICAO_ID = {sql_quote(icao)}"]
    inferred = infer_faa_id_from_icao(icao)
    for faa_id in [extra_faa_id, inferred]:
        if faa_id:
            clauses.append(f"FAA_ID = {sql_quote(faa_id.strip().upper())}")
    return " OR ".join(f"({c})" for c in dict.fromkeys(clauses))


# ----------------------------- AirNav magnetic/true runway headings -----------------------------


def html_to_loose_text(raw_html: str) -> str:
    """Convert HTML to loose searchable text without external parsers."""
    s = re.sub(r"(?is)<script.*?</script>|<style.*?</style>", " ", raw_html)
    s = re.sub(r"(?i)<br\s*/?>", "\n", s)
    s = re.sub(r"(?i)</(h\d|p|div|tr|table|li|ul|ol)>", "\n", s)
    s = re.sub(r"<[^>]+>", " ", s)
    s = html.unescape(s).replace("\xa0", " ")
    s = re.sub(r"[ \t\r\f\v]+", " ", s)
    s = re.sub(r"\n\s+", "\n", s)
    s = re.sub(r"\n{2,}", "\n", s)
    return s.strip()


def build_airnav_heading_index(icao: str, timeout: float) -> dict[frozenset[str], dict[str, dict[str, str]]]:
    """
    Scrape AirNav runway heading lines.

    Return shape:
      {frozenset({'4L','22R'}): {'4L': {'magnetic':'044', 'true':'031'}, ...}}
    """
    url = AIRNAV_URL_TEMPLATE.format(code=urllib.parse.quote(icao.lower()))
    text = html_to_loose_text(http_text(url, timeout=timeout))

    # Example section in text:
    #   Runway 13L/31R ... Runway heading: 134 magnetic, 121 true 314 magnetic, 301 true
    runway_re = re.compile(r"\bRunway\s+([0-3]?\d[LCR]?/[0-3]?\d[LCR]?)\b", re.IGNORECASE)
    heading_re = re.compile(
        r"Runway\s+heading:\s*"
        r"(\d{3})\s+magnetic,\s*(\d{3})\s+true\s+"
        r"(\d{3})\s+magnetic,\s*(\d{3})\s+true",
        re.IGNORECASE,
    )

    matches = list(runway_re.finditer(text))
    out: dict[frozenset[str], dict[str, dict[str, str]]] = {}

    for idx, m in enumerate(matches):
        pair = m.group(1)
        ends = runway_pair_order(pair)
        if len(ends) != 2:
            continue

        start = m.end()
        end = matches[idx + 1].start() if idx + 1 < len(matches) else len(text)
        section = text[start:end]

        hm = heading_re.search(section)
        if not hm:
            # Some stripped pages can put the pair title very close to the heading label;
            # include the title itself in a second attempt.
            hm = heading_re.search(text[m.start():end])
        if not hm:
            continue

        mag_a, true_a, mag_b, true_b = hm.groups()
        out[frozenset(ends)] = {
            ends[0]: {"magnetic": mag_a, "true": true_a},
            ends[1]: {"magnetic": mag_b, "true": true_b},
        }

    return out


def heading_from_airnav_index(
    rwy_id: str,
    airnav_index: dict[frozenset[str], dict[str, dict[str, str]]],
    reference: str,
) -> str | None:
    ordered = runway_pair_order(rwy_id)
    if len(ordered) != 2:
        return None
    rows = airnav_index.get(frozenset(ordered))
    if not rows:
        return None

    a = rows.get(ordered[0], {}).get(reference)
    b = rows.get(ordered[1], {}).get(reference)
    if not a or not b:
        return None
    return f"{int(a) % 360:03d}/{int(b) % 360:03d}"


# ----------------------------- BTS/NTAD true alignment + magnetic variation -----------------------------


def build_bts_runway_end_index(faa_ids: set[str], timeout: float) -> dict[tuple[str, frozenset[str]], list[dict[str, Any]]]:
    if not faa_ids:
        return {}

    where = "ARPT_ID IN (" + ",".join(sql_quote(x) for x in sorted(faa_ids)) + ")"
    rows = query_table_rows(
        BTS_RUNWAY_ENDS_TABLE_URL,
        where,
        ["OBJECTID", "ARPT_ID", "RWY_ID", "RWY_END_ID", "TRUE_ALIGNMENT"],
        timeout,
    )

    index: dict[tuple[str, frozenset[str]], list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        arpt_id = str(row.get("ARPT_ID") or "").strip().upper()
        key = runway_pair_key(str(row.get("RWY_ID") or ""))
        if arpt_id and key:
            index[(arpt_id, key)].append(row)
    return dict(index)


def true_tracks_from_bts_rows(rwy_id: str, rows: list[dict[str, Any]]) -> tuple[float, float] | None:
    if not rows:
        return None

    by_end: dict[str, dict[str, Any]] = {}
    for row in rows:
        end_id = normalize_runway_end_id(row.get("RWY_END_ID"))
        if end_id:
            by_end[end_id] = row

    ordered = runway_pair_order(rwy_id)
    if len(ordered) != 2:
        return None

    def parse(row: dict[str, Any] | None) -> float | None:
        if row is None:
            return None
        try:
            return float(row.get("TRUE_ALIGNMENT")) % 360.0
        except (TypeError, ValueError):
            return None

    a = parse(by_end.get(ordered[0]))
    b = parse(by_end.get(ordered[1]))

    if a is None and b is not None:
        a = (b + 180.0) % 360.0
    if b is None and a is not None:
        b = (a + 180.0) % 360.0
    if a is None or b is None:
        return None
    return a, b


def first_existing(row: dict[str, Any], names: list[str]) -> Any:
    for name in names:
        value = row.get(name)
        if value not in (None, ""):
            return value
    return None


def parse_magvar_degrees(row: dict[str, Any]) -> float | None:
    """
    Return magnetic variation with east positive, west negative.
    Magnetic heading = true heading - variation.
    Example: JFK true 031, variation 13W => 031 - (-13) = 044 magnetic.
    """
    raw_var = first_existing(row, ["MAG_VARN", "MAG_VAR", "MAGVAR", "MAGNETIC_VARIATION"])
    raw_hemis = first_existing(row, ["MAG_HEMIS", "MAG_HEMI", "MAGVAR_HEMIS", "MAG_VAR_HEMIS"])
    if raw_var is None:
        return None

    try:
        val = float(raw_var)
    except (TypeError, ValueError):
        return None

    hemis = str(raw_hemis or "").strip().upper()
    if hemis.startswith("W"):
        return -abs(val)
    if hemis.startswith("E"):
        return abs(val)
    return val


def build_magvar_index(faa_ids: set[str], icao: str, timeout: float) -> dict[str, float]:
    clauses: list[str] = []
    if faa_ids:
        clauses.append("ARPT_ID IN (" + ",".join(sql_quote(x) for x in sorted(faa_ids)) + ")")
    if icao:
        clauses.append(f"ICAO_ID = {sql_quote(icao)}")
    if not clauses:
        return {}

    rows = query_table_rows(
        BTS_AVIATION_FACILITIES_URL,
        " OR ".join(f"({c})" for c in clauses),
        ["OBJECTID", "ARPT_ID", "ICAO_ID", "MAG_VARN", "MAG_HEMIS", "MAG_VARN_YEAR"],
        timeout,
    )

    out: dict[str, float] = {}
    for row in rows:
        magvar = parse_magvar_degrees(row)
        if magvar is None:
            continue
        arpt_id = str(row.get("ARPT_ID") or "").strip().upper()
        icao_id = str(row.get("ICAO_ID") or "").strip().upper()
        if arpt_id:
            out[arpt_id] = magvar
        if icao_id:
            out[icao_id] = magvar
    return out


def heading_from_bts(
    rwy_id: str,
    faa_id: str,
    bts_end_index: dict[tuple[str, frozenset[str]], list[dict[str, Any]]],
    magvar_index: dict[str, float],
    reference: str,
    icao: str,
) -> str | None:
    rows = bts_end_index.get((faa_id, runway_pair_key(rwy_id)), [])
    true_tracks = true_tracks_from_bts_rows(rwy_id, rows)
    if true_tracks is None:
        return None

    if reference == "true":
        return format_track_pair(true_tracks[0], true_tracks[1])

    magvar = magvar_index.get(faa_id)
    if magvar is None:
        magvar = magvar_index.get(icao)
    if magvar is None:
        return None

    return format_track_pair((true_tracks[0] - magvar) % 360.0, (true_tracks[1] - magvar) % 360.0)


# ----------------------------- optional geometry fallback -----------------------------


def bearing_deg(lon1: float, lat1: float, lon2: float, lat2: float) -> float:
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dlon = math.radians(lon2 - lon1)
    y = math.sin(dlon) * math.cos(phi2)
    x = math.cos(phi1) * math.sin(phi2) - math.sin(phi1) * math.cos(phi2) * math.cos(dlon)
    return (math.degrees(math.atan2(y, x)) + 360.0) % 360.0


def angle_diff(a: float, b: float) -> float:
    return abs((a - b + 180.0) % 360.0 - 180.0)


def runway_designator_hint(rwy_id: str) -> float | None:
    m = re.match(r"^\s*(\d{1,2})", str(rwy_id))
    if not m:
        return None
    n = int(m.group(1))
    if not (1 <= n <= 36):
        return None
    return 0.0 if n == 36 else float(n * 10)


def runway_centerline_track_from_polygon(ring: list[list[float]], rwy_id: str) -> tuple[float, float]:
    """Estimate true centerline track from polygon using a local PCA axis."""
    pts = open_ring(ring)
    if len(pts) < 2:
        raise ValueError("Need at least two polygon points")

    lon0 = sum(p[0] for p in pts) / len(pts)
    lat0 = sum(p[1] for p in pts) / len(pts)
    cos_lat0 = max(math.cos(math.radians(lat0)), 1e-12)
    earth_r = 6371008.8

    xy: list[tuple[float, float]] = []
    for lon, lat in pts:
        x = earth_r * math.radians(lon - lon0) * cos_lat0
        y = earth_r * math.radians(lat - lat0)
        xy.append((x, y))

    mx = sum(x for x, _ in xy) / len(xy)
    my = sum(y for _, y in xy) / len(xy)
    sxx = sum((x - mx) * (x - mx) for x, _ in xy)
    syy = sum((y - my) * (y - my) for _, y in xy)
    sxy = sum((x - mx) * (y - my) for x, y in xy)

    theta = 0.5 * math.atan2(2.0 * sxy, sxx - syy)
    ux, uy = math.cos(theta), math.sin(theta)
    projections = [((x - mx) * ux + (y - my) * uy, x, y) for x, y in xy]
    _, x1, y1 = min(projections, key=lambda t: t[0])
    _, x2, y2 = max(projections, key=lambda t: t[0])

    def unproject(x: float, y: float) -> list[float]:
        lon = lon0 + math.degrees(x / (earth_r * cos_lat0))
        lat = lat0 + math.degrees(y / earth_r)
        return [lon, lat]

    p1 = unproject(x1, y1)
    p2 = unproject(x2, y2)
    trk = bearing_deg(p1[0], p1[1], p2[0], p2[1])
    opp = (trk + 180.0) % 360.0

    hint = runway_designator_hint(rwy_id)
    if hint is not None and angle_diff(opp, hint) < angle_diff(trk, hint):
        trk, opp = opp, trk
    return trk, opp


# ----------------------------- assembly -----------------------------


def resolve_missing_value(value: str) -> Any:
    if value.lower() == "null":
        return None
    return value


def resolve_heading(
    rwy_id: str,
    faa_id: str,
    icao: str,
    reference: str,
    heading_source: str,
    airnav_index: dict[frozenset[str], dict[str, dict[str, str]]],
    bts_end_index: dict[tuple[str, frozenset[str]], list[dict[str, Any]]],
    magvar_index: dict[str, float],
) -> str | None:
    if heading_source in {"auto", "airnav"}:
        h = heading_from_airnav_index(rwy_id, airnav_index, reference)
        if h is not None:
            return h
        if heading_source == "airnav":
            return None

    if heading_source in {"auto", "bts"}:
        return heading_from_bts(rwy_id, faa_id, bts_end_index, magvar_index, reference, icao)

    return None


def build_airport_json(
    icao: str,
    runway_url: str,
    taxiway_url: str,
    timeout: float,
    digits: int,
    faa_id_arg: str | None = None,
    include_track: bool = True,
    track_reference: str = "magnetic",
    heading_source: str = "auto",
    missing_track_value: str = "NA",
    track_fallback: str = "none",
    include_holdbars: bool = True,
    holdbar_distance_m: float = 10.0,
    holdbar_angle_deg: float = 15.0,
    include_gates: bool = True,
    overpass_url: str = OVERPASS_INTERPRETER_URL,
    overpass_timeout: float = 60.0,
    gate_bbox_padding_m: float = 300.0,
    user_agent: str | None = None,
) -> dict[str, Any]:
    where = airport_where_clause(icao, faa_id_arg)

    rwy_features = query_features(
        runway_url,
        where,
        ["OBJECTID", "RWY_ID", "DESIGNATOR", "FAA_ID", "ICAO_ID"],
        timeout,
    )
    twy_features = query_features(
        taxiway_url,
        where,
        ["OBJECTID", "DESIGNATOR", "TWY_OPER", "FAA_ID", "ICAO_ID"],
        timeout,
    )

    if not rwy_features and not twy_features:
        raise SurfaceFetchError(
            f"No AM_Runway or AM_Taxiway features found for {icao}. "
            "Try --faa-id, or override --runway-url/--taxiway-url."
        )

    faa_ids: set[str] = set()
    if faa_id_arg:
        faa_ids.add(faa_id_arg.strip().upper())
    inferred = infer_faa_id_from_icao(icao)
    if inferred:
        faa_ids.add(inferred)

    for feat in rwy_features:
        props = feat.get("properties") or {}
        faa_id = str(props.get("FAA_ID") or "").strip().upper()
        if faa_id:
            faa_ids.add(faa_id)

    airnav_index: dict[frozenset[str], dict[str, dict[str, str]]] = {}
    bts_end_index: dict[tuple[str, frozenset[str]], list[dict[str, Any]]] = {}
    magvar_index: dict[str, float] = {}

    if include_track and heading_source in {"auto", "airnav"}:
        try:
            airnav_index = build_airnav_heading_index(icao, timeout)
            if not airnav_index and heading_source == "airnav":
                print(f"warning: AirNav returned no runway headings for {icao}", file=sys.stderr)
        except Exception as exc:
            print(f"warning: AirNav heading lookup failed for {icao}: {exc}", file=sys.stderr)
            airnav_index = {}

    if include_track and heading_source in {"auto", "bts"}:
        try:
            bts_end_index = build_bts_runway_end_index(faa_ids, timeout)
        except Exception as exc:
            print(f"warning: BTS runway-end lookup failed: {exc}", file=sys.stderr)
            bts_end_index = {}

        # Required for magnetic conversion; harmless for true reference.
        try:
            magvar_index = build_magvar_index(faa_ids, icao, timeout)
        except Exception as exc:
            print(f"warning: BTS airport magnetic-variation lookup failed: {exc}", file=sys.stderr)
            magvar_index = {}

    out: dict[str, Any] = {"icao": icao, "rwys": [], "twys": [], "hbs": [], "gates": []}
    missing_value = resolve_missing_value(missing_track_value)
    runway_entries: list[dict[str, Any]] = []
    taxiway_entries: list[dict[str, Any]] = []

    for feat in rwy_features:
        props = feat.get("properties") or {}
        rid = str(props.get("RWY_ID") or props.get("DESIGNATOR") or "").strip()
        if not rid:
            rid = f"OBJECTID-{props.get('OBJECTID', 'unknown')}"
        faa_id = str(props.get("FAA_ID") or faa_id_arg or inferred or "").strip().upper()

        heading = None
        if include_track:
            heading = resolve_heading(
                rwy_id=rid,
                faa_id=faa_id,
                icao=icao,
                reference=track_reference,
                heading_source=heading_source,
                airnav_index=airnav_index,
                bts_end_index=bts_end_index,
                magvar_index=magvar_index,
            )

        for ring in exterior_rings(feat.get("geometry")):
            try:
                runway_polygon = runway_outer_corner_polygon(ring)
            except Exception as exc:
                print(
                    f"warning: runway corner extraction failed for runway {rid}; "
                    f"using full polygon: {exc}",
                    file=sys.stderr,
                )
                runway_polygon = ring

            item: dict[str, Any] = {"id": rid, "polygon": round_ring(runway_polygon, digits)}

            if include_track:
                final_heading = heading

                if final_heading is None and track_fallback == "geometry":
                    try:
                        true_a, true_b = runway_centerline_track_from_polygon(ring, rid)
                        if track_reference == "true":
                            final_heading = format_track_pair(true_a, true_b)
                        else:
                            magvar = magvar_index.get(faa_id) or magvar_index.get(icao)
                            if magvar is not None:
                                final_heading = format_track_pair((true_a - magvar) % 360.0, (true_b - magvar) % 360.0)
                    except Exception as exc:
                        print(f"warning: geometry fallback failed for runway {rid}: {exc}", file=sys.stderr)

                if final_heading is not None:
                    item["track"] = final_heading
                elif missing_track_value.lower() != "omit":
                    item["track"] = missing_value

            out["rwys"].append(item)
            runway_entries.append({"id": rid, "ring": ring})

    for feat in twy_features:
        props = feat.get("properties") or {}
        tid = str(props.get("DESIGNATOR") or "").strip()
        if not tid:
            tid = f"OBJECTID-{props.get('OBJECTID', 'unknown')}"

        for ring in exterior_rings(feat.get("geometry")):
            out["twys"].append({"id": tid, "polygon": round_ring(ring, digits)})
            taxiway_entries.append({"id": tid, "ring": ring})

    if include_holdbars:
        out["hbs"] = derive_holdbars(
            runway_entries=runway_entries,
            taxiway_entries=taxiway_entries,
            digits=digits,
            max_distance_m=holdbar_distance_m,
            max_angle_deg=holdbar_angle_deg,
        )

    if include_gates:
        try:
            out["gates"] = fetch_osm_gates(
                icao=icao,
                digits=digits,
                overpass_url=overpass_url,
                overpass_timeout=overpass_timeout,
                user_agent=user_agent,
            )
        except Exception as exc:
            print(f"warning: OSM/Overpass gate lookup failed for {icao}: {exc}", file=sys.stderr)
            out["gates"] = []

    out["rwys"].sort(key=lambda x: natural_key(str(x.get("id", ""))))
    out["twys"].sort(key=lambda x: natural_key(str(x.get("id", ""))))
    out["hbs"].sort(key=lambda x: natural_key(str(x.get("id", ""))))
    out["gates"].sort(key=lambda x: natural_key(str(x.get("id", ""))))
    return out


# ----------------------------- CLI -----------------------------


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Fetch FAA AM_Runway/AM_Taxiway polygons and runway heading metadata."
    )
    p.add_argument("icao", help="Four-letter ICAO airport code, e.g. KJFK")
    p.add_argument("--faa-id", help="Optional FAA/location id override, e.g. JFK for KJFK")
    p.add_argument("--out", help="Optional output file. Default: stdout")
    p.add_argument("--indent", type=int, default=2, help="JSON indent. Use 0 for compact JSON")
    p.add_argument("--digits", type=int, default=8, help="Decimal coordinate digits")
    p.add_argument("--timeout", type=float, default=30.0, help="HTTP timeout in seconds")
    p.add_argument("--pending", action="store_true", help="Allow Pending_AM_* services if discovered")
    p.add_argument("--no-track", action="store_true", help="Do not include runway track metadata")
    p.add_argument(
        "--track-reference",
        choices=("magnetic", "true"),
        default="magnetic",
        help="Heading reference for runway track. Default: magnetic.",
    )
    p.add_argument(
        "--heading-source",
        choices=("auto", "airnav", "bts", "none"),
        default="auto",
        help="auto=AirNav first, then BTS/NTAD fallback. Default: auto.",
    )
    p.add_argument(
        "--missing-track-value",
        default="NA",
        help="Value used if heading is unresolved. Use 'null' for JSON null or 'omit' to omit the field.",
    )
    p.add_argument(
        "--track-fallback",
        choices=("none", "geometry"),
        default="none",
        help="Optional geometry-derived fallback. Default: none.",
    )
    p.add_argument("--no-holdbars", action="store_true", help="Do not derive holdbar edges")
    p.add_argument(
        "--holdbar-distance-m",
        type=float,
        default=10.0,
        help="Maximum holdbar edge distance to runway in meters. Default: 10.0.",
    )
    p.add_argument(
        "--holdbar-angle-deg",
        type=float,
        default=15.0,
        help="Maximum angular difference from runway heading in degrees. Default: 15.0.",
    )
    p.add_argument("--no-gates", action="store_true", help="Do not fetch OSM aeroway=gate elements via Overpass")
    p.add_argument(
        "--overpass-url",
        default=OVERPASS_INTERPRETER_URL,
        help="Overpass API interpreter URL used for gates. Default: overpass-api.de.",
    )
    p.add_argument(
        "--overpass-timeout",
        type=float,
        default=60.0,
        help="Overpass request timeout in seconds. Default: 60.0.",
    )
    p.add_argument(
        "--user-agent",
        default=DEFAULT_USER_AGENT,
        help=(
            "Custom identifying User-Agent for HTTP requests, especially Overpass. "
            "Do not use Mozilla/5.0. Prefer 'project/1.0 (email-or-project-url)'."
        ),
    )
    p.add_argument(
        "--gate-bbox-padding-m",
        type=float,
        default=300.0,
        help="Deprecated/ignored: gate lookup now uses Overpass around.apt:8000.",
    )
    p.add_argument(
        "--print-overpass-query",
        action="store_true",
        help="Print the Overpass Turbo query for this ICAO and exit.",
    )
    p.add_argument("--runway-url", help="Override AM_Runway FeatureServer root URL")
    p.add_argument("--taxiway-url", help="Override AM_Taxiway FeatureServer root URL")
    return p.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    global USER_AGENT

    args = parse_args(argv)
    USER_AGENT = args.user_agent.strip() or DEFAULT_USER_AGENT
    icao = args.icao.strip().upper()

    if not re.fullmatch(r"[A-Z0-9]{4}", icao):
        print(f"Invalid ICAO code: {args.icao!r}", file=sys.stderr)
        return 2

    if args.print_overpass_query:
        print(build_overpass_gates_query(icao, int(args.overpass_timeout)))
        return 0

    try:
        runway_url = choose_service_url("AM_Runway", args.runway_url, args.pending, args.timeout)
        taxiway_url = choose_service_url("AM_Taxiway", args.taxiway_url, args.pending, args.timeout)
        result = build_airport_json(
            icao=icao,
            runway_url=runway_url,
            taxiway_url=taxiway_url,
            timeout=args.timeout,
            digits=args.digits,
            faa_id_arg=args.faa_id,
            include_track=not args.no_track,
            track_reference=args.track_reference,
            heading_source=args.heading_source,
            missing_track_value=args.missing_track_value,
            track_fallback=args.track_fallback,
            include_holdbars=not args.no_holdbars,
            holdbar_distance_m=args.holdbar_distance_m,
            holdbar_angle_deg=args.holdbar_angle_deg,
            include_gates=not args.no_gates,
            overpass_url=args.overpass_url,
            overpass_timeout=args.overpass_timeout,
            gate_bbox_padding_m=args.gate_bbox_padding_m,
            user_agent=USER_AGENT,
        )
    except SurfaceFetchError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    indent = None if args.indent == 0 else args.indent
    text = json.dumps(result, indent=indent, separators=(",", ":") if indent is None else None)

    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)
            f.write("\n")
    else:
        print(text)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
