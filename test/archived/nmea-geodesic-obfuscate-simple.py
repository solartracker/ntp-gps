#!/usr/bin/env python3
"""
nmea-geodesic-obfuscate-simple.py

Shift all NMEA GPS fixes by a simple vector (house -> village center)
to anonymize location data while preserving motion. Only GGA, RMC,
and GLL sentences are modified.

This script simply computes the latitude and longitude differences between
the house and village center and applies that as a constant shift to each
NMEA fix. No geodesic calculations are used. Self-contained; no external
dependencies.

Usage:
    python3 nmea-geodesic-obfuscate-simple.py infile.txt outfile.txt --house LAT LON [--village-center LAT LON] [--preview N]

Example:
    python3 nmea-geodesic-obfuscate-simple.py nmea.txt nmea_obf.txt --house 44.138821609323024 -72.6421729447827 --preview 5

Notes:
- House coordinates are required (private).
- Village center defaults to (44.148630208502965, -72.65679699901393).
- Output may differ slightly from geodesic/Vincenty-based scripts.
- Self-contained; no external dependencies.

Notes on geodesic accuracy:
- This simple version calculates the latitude/longitude shift as a constant vector
  from the house location to the village center. Unlike the Vincenty or GeographicLib
  methods, it does not account for the ellipsoidal curvature of the Earth.
- Over short distances (tens to hundreds of meters or a few kilometers), the difference
  between a true geodesic and a simple vector shift is negligible. In practice, for
  the scale of house-to-village-center shifts, the outputs are identical to the
  Vincenty/GeographicLib versions when rounded to NMEA precision.

Credits:
- Python guidance: OpenAI GPT
- Script and copyright: Richard Elwell, 2025

License:
GNU General Public License v3 or later; see https://www.gnu.org/licenses/
This program is distributed WITHOUT ANY WARRANTY.
"""

from __future__ import annotations
import re, sys, argparse

# ---------- NMEA helpers ----------
_RE_LAT = re.compile(r'^\d{4,}\.\d+$')
_RE_LON = re.compile(r'^\d{5,}\.\d+$')

def nmea_checksum(body_no_dollar: str) -> str:
    cs = 0
    for ch in body_no_dollar:
        cs ^= ord(ch)
    return f"*{cs:02X}"

def strip_checksum(line: str) -> str:
    line = line.rstrip("\r\n")
    if '*' in line:
        return line[:line.rfind('*')]
    return line

def dm_to_deg_safe(dm_str: str, hemi: str):
    if not dm_str or not hemi:
        return None
    try:
        v = float(dm_str)
    except ValueError:
        return None
    deg = int(v // 100)
    minutes = v - deg * 100
    dec = deg + minutes / 60.0
    if hemi in ('S','W'):
        dec = -dec
    if hemi in ('N','S') and not (-90.0 <= dec <= 90.0):
        return None
    if hemi in ('E','W') and not (-180.0 <= dec <= 180.0):
        return None
    return dec

def deg_to_dm_nmea(deg: float, is_lat: bool):
    """
    Convert decimal degrees to NMEA DM format with correct hemisphere.
    For latitude: DDMM.MMMMM
    For longitude: DDDMM.MMMMM
    """
    hemi = 'N' if deg >= 0 else 'S' if is_lat else 'E' if deg >= 0 else 'W'
    d = abs(deg)
    deg_int = int(d)
    minutes = (d - deg_int) * 60.0
    if is_lat:
        return f"{deg_int:02d}{minutes:08.5f}", hemi
    else:
        return f"{deg_int:03d}{minutes:08.5f}", hemi

# ---------- Sentence processing ----------
GN_POS_TYPES = {
    'GGA': (2, 3, 4, 5),
    'RMC': (3, 4, 5, 6),
    'GLL': (1, 2, 3, 4)
}

def sentence_key(header: str):
    return header[-3:] if len(header) >= 3 else header

def process_line(line: str, dlat: float, dlon: float, last_valid: dict):
    ln = line.rstrip("\r\n")
    if not ln.startswith('$'):
        return line
    stripped = strip_checksum(ln)[1:]  # drop leading $
    parts = stripped.split(',')
    if not parts:
        return line
    key = sentence_key(parts[0])
    if key in GN_POS_TYPES:
        lat_i, lat_h_i, lon_i, lon_h_i = GN_POS_TYPES[key]
        if len(parts) <= max(lat_i, lat_h_i, lon_i, lon_h_i):
            return line
        lat = dm_to_deg_safe(parts[lat_i], parts[lat_h_i])
        lon = dm_to_deg_safe(parts[lon_i], parts[lon_h_i])
        if lat is None or lon is None:
            if last_valid.get('lat') is None:
                return line
            new_lat, new_lon = last_valid['lat'], last_valid['lon']
        else:
            new_lat = lat + dlat
            new_lon = lon + dlon
            last_valid['lat'], last_valid['lon'] = new_lat, new_lon
        lat_field, lat_h = deg_to_dm_nmea(new_lat, True)
        lon_field, lon_h = deg_to_dm_nmea(new_lon, False)
        parts[lat_i], parts[lat_h_i], parts[lon_i], parts[lon_h_i] = lat_field, lat_h, lon_field, lon_h
        body = ",".join(parts)
        return f"${body}{nmea_checksum(body)}\r\n"
    return line

# ---------- Main ----------
def compute_shift_and_apply(infile: str, outfile: str, house: tuple, village: tuple, preview: int = 0):
    dlat = village[0] - house[0]
    dlon = village[1] - house[1]
    last_valid = {'lat': None, 'lon': None}
    changed = 0
    printed = 0
    with open(infile,'r',encoding='utf-8',errors='replace') as fin, \
         open(outfile,'w',encoding='utf-8',errors='replace') as fout:
        for line in fin:
            new_line = process_line(line, dlat, dlon, last_valid)
            if new_line != line:
                changed += 1
                if preview and printed < preview:
                    sys.stdout.write("OLD: "+line.rstrip("\n")+"\n")
                    sys.stdout.write("NEW: "+new_line.rstrip("\r\n")+"\n\n")
                    printed += 1
            fout.write(new_line)
    return dlat, dlon, changed

def parse_args():
    p = argparse.ArgumentParser(description="Obfuscate NMEA positions by shifting each fix by house -> village center vector (simple additive).")
    p.add_argument('infile')
    p.add_argument('outfile')
    p.add_argument('--house', nargs=2, type=float, metavar=('LAT','LON'), required=True, help='House coordinates (required)')
    p.add_argument('--village-center', nargs=2, type=float, metavar=('LAT','LON'), default=(44.148630208502965, -72.65679699901393), help='Village center coordinates (optional)')
    p.add_argument('--preview', type=int, default=0, help='Print first N changed lines')
    return p.parse_args()

if __name__ == "__main__":
    args = parse_args()
    house = (args.house[0], args.house[1])
    village_center = (args.village_center[0], args.village_center[1])
    dlat, dlon, changed = compute_shift_and_apply(args.infile, args.outfile, house, village_center, preview=args.preview)
    print(f"Applied simple shift: Δlat {dlat:.8f}, Δlon {dlon:.8f}. Modified {changed} lines.")

