#!/usr/bin/env python3
"""
nmea-geodesic-obfuscate.py

Shift all NMEA GPS fixes by a geodesic vector (house -> village center) 
to anonymize location data while preserving realistic motion. Only GGA, RMC, 
and GLL sentences are modified.

This script implements direct and inverse geodesic calculations using algorithms
based on Charles Karney's work in GeographicLib. All code is self-contained
and has no external dependencies.

Usage:
    python3 nmea-geodesic-obfuscate.py infile.txt outfile.txt --house LAT LON [--village-center LAT LON] [--preview N]

Example:
    python3 nmea-geodesic-obfuscate.py nmea.txt nmea_obf.txt --house 44.138821609323024 -72.6421729447827 --preview 5

Notes:
- House coordinates are required (private). 
- Village center defaults to (44.148630208502965, -72.65679699901393).
- Vincenty-like geodesics based on Charles Karney's GeographicLib.
- Self-contained; no external dependencies.

Credits:
- Geodesic algorithm: Charles Karney, GeographicLib (C++), https://geographiclib.sourceforge.io/
- Python guidance: OpenAI GPT
- Script and copyright: Richard Elwell, 2025

License:
GNU General Public License v3 or later; see https://www.gnu.org/licenses/
This program is distributed WITHOUT ANY WARRANTY.
"""

from __future__ import annotations
import math, re, sys, argparse

# ---------- WGS84 ----------
_a = 6378137.0
_f = 1.0 / 298.257223563
_b = _a * (1.0 - _f)

# ---------- Geodesic inverse/direct (Vincenty-like) ----------
def geodesic_inverse(lat1, lon1, lat2, lon2, tol=1e-12, maxiter=200):
    if (lat1 == lat2) and (lon1 == lon2):
        return 0.0, 0.0
    phi1 = math.radians(lat1); phi2 = math.radians(lat2)
    L = math.radians(lon2 - lon1)
    U1 = math.atan((1.0 - _f) * math.tan(phi1))
    U2 = math.atan((1.0 - _f) * math.tan(phi2))
    sinU1, cosU1 = math.sin(U1), math.cos(U1)
    sinU2, cosU2 = math.sin(U2), math.cos(U2)
    lamb = L
    for _ in range(maxiter):
        sin_lamb = math.sin(lamb); cos_lamb = math.cos(lamb)
        sin_sigma = math.sqrt(
            (cosU2 * sin_lamb) ** 2 +
            (cosU1 * sinU2 - sinU1 * cosU2 * cos_lamb) ** 2
        )
        if sin_sigma == 0:
            return 0.0, 0.0
        cos_sigma = sinU1 * sinU2 + cosU1 * cosU2 * cos_lamb
        sigma = math.atan2(sin_sigma, cos_sigma)
        sin_alpha = cosU1 * cosU2 * sin_lamb / sin_sigma
        cos2_alpha = 1.0 - sin_alpha * sin_alpha
        cos2_sigma_m = (cos_sigma - 2.0 * sinU1 * sinU2 / cos2_alpha) if cos2_alpha != 0 else 0.0
        C = (_f / 16.0) * cos2_alpha * (4.0 + _f * (4.0 - 3.0 * cos2_alpha))
        lamb_prev = lamb
        lamb = L + (1.0 - C) * _f * sin_alpha * (
            sigma + C * sin_sigma * (cos2_sigma_m + C * cos_sigma * (-1.0 + 2.0 * cos2_sigma_m * cos2_sigma_m))
        )
        if abs(lamb - lamb_prev) < tol:
            break
    else:
        raise RuntimeError("geodesic_inverse failed to converge")
    u2 = cos2_alpha * ((_a * _a - _b * _b) / (_b * _b))
    A = 1.0 + u2 / 16384.0 * (4096.0 + u2 * (-768.0 + u2 * (320.0 - 175.0 * u2)))
    B = u2 / 1024.0 * (256.0 + u2 * (-128.0 + u2 * (74.0 - 47.0 * u2)))
    delta_sigma = B * sin_sigma * (
        cos2_sigma_m + B / 4.0 * (cos_sigma * (-1.0 + 2.0 * cos2_sigma_m * cos2_sigma_m) -
        B / 6.0 * cos2_sigma_m * (-3.0 + 4.0 * sin_sigma * sin_sigma) * (-3.0 + 4.0 * cos2_sigma_m * cos2_sigma_m))
    )
    s = _b * A * (sigma - delta_sigma)
    alpha1 = math.atan2(cosU2 * math.sin(lamb),
                        cosU1 * sinU2 - sinU1 * cosU2 * math.cos(lamb))
    az12 = (math.degrees(alpha1) + 360.0) % 360.0
    return s, az12

def geodesic_direct(lat1, lon1, az12_deg, s, tol=1e-12, maxiter=200):
    if s == 0:
        return lat1, lon1
    phi1 = math.radians(lat1)
    alpha1 = math.radians(az12_deg)
    sin_alpha1 = math.sin(alpha1); cos_alpha1 = math.cos(alpha1)
    tanU1 = (1.0 - _f) * math.tan(phi1)
    cosU1 = 1.0 / math.sqrt(1.0 + tanU1 * tanU1)
    sinU1 = tanU1 * cosU1
    sigma1 = math.atan2(tanU1, cos_alpha1)
    sin_alpha = cosU1 * sin_alpha1
    cos2_alpha = 1.0 - sin_alpha * sin_alpha
    u2 = cos2_alpha * ((_a * _a - _b * _b) / (_b * _b))
    A = 1.0 + u2 / 16384.0 * (4096.0 + u2 * (-768.0 + u2 * (320.0 - 175.0 * u2)))
    B = u2 / 1024.0 * (256.0 + u2 * (-128.0 + u2 * (74.0 - 47.0 * u2)))
    sigma = s / (_b * A)
    for _ in range(maxiter):
        two_sigma_m = 2.0 * sigma1 + sigma
        sin_sigma = math.sin(sigma); cos_sigma = math.cos(sigma)
        delta_sigma = B * sin_sigma * (
            math.cos(two_sigma_m) + B / 4.0 * (
                cos_sigma * (-1.0 + 2.0 * math.cos(two_sigma_m) ** 2) -
                B / 6.0 * math.cos(two_sigma_m) * (-3.0 + 4.0 * sin_sigma ** 2) *
                (-3.0 + 4.0 * math.cos(two_sigma_m) ** 2)
            )
        )
        sigma_prev = sigma
        sigma = s / (_b * A) + delta_sigma
        if abs(sigma - sigma_prev) < tol:
            break
    tmp = sinU1 * math.sin(sigma) - cosU1 * math.cos(sigma) * cos_alpha1
    lat2 = math.atan2(sinU1 * math.cos(sigma) + cosU1 * math.sin(sigma) * cos_alpha1,
                      (1.0 - _f) * math.sqrt(sin_alpha * sin_alpha + tmp * tmp))
    lam = math.atan2(math.sin(sigma) * sin_alpha1,
                     cosU1 * math.cos(sigma) - sinU1 * math.sin(sigma) * cos_alpha1)
    cos2_alpha = max(0.0, 1.0 - sin_alpha * sin_alpha)
    C = (_f / 16.0) * cos2_alpha * (4.0 + _f * (4.0 - 3.0 * cos2_alpha))
    L = lam - (1.0 - C) * _f * sin_alpha * (
        sigma + C * math.sin(sigma) * (math.cos(2.0 * sigma1 + sigma) +
                                        C * math.cos(sigma) * (-1.0 + 2.0 * math.cos(2.0 * sigma1 + sigma) ** 2))
    )
    lon2 = math.degrees(L + math.radians(lon1))
    lat2_deg = math.degrees(lat2)
    lon2 = ((lon2 + 180.0) % 360.0) - 180.0
    return lat2_deg, lon2

# -------------------- NMEA helpers --------------------
_RE_LAT = re.compile(r'^\d{4,}\.\d+$')
_RE_LON = re.compile(r'^\d{5,}\.\d+$')

def nmea_checksum(body: str) -> str:
    cs = 0
    for ch in body:
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
    minutes = v - deg*100
    dec = deg + minutes/60
    if hemi in ('S','W'):
        dec = -dec
    if hemi in ('N','S') and not (-90 <= dec <= 90):
        return None
    if hemi in ('E','W') and not (-180 <= dec <= 180):
        return None
    return dec

def deg_to_dm_nmea(deg: float, is_lat: bool):
    """
    Convert decimal degrees to NMEA DM format.
    Latitude: DDMM.MMMMM
    Longitude: DDDMM.MMMMM
    """
    hemi = 'N' if deg>=0 else 'S' if is_lat else 'E' if deg>=0 else 'W'
    d = abs(deg)
    deg_int = int(d)
    minutes = (d - deg_int)*60
    if is_lat:
        return (f"{deg_int:02d}{minutes:08.5f}", hemi)
    else:
        return (f"{deg_int:03d}{minutes:08.5f}", hemi)

# -------------------- Sentence processing --------------------
GN_POS_TYPES = {
    'GGA': (2,3,4,5),
    'RMC': (3,4,5,6),
    'GLL': (1,2,3,4)
}

def sentence_key(header: str):
    return header[-3:] if len(header) >= 3 else header

def process_line(line: str, distance_m: float, az_deg: float, last_valid: dict):
    ln = line.rstrip("\r\n")
    if not ln.startswith('$'):
        return line
    stripped = strip_checksum(ln)[1:]
    parts = stripped.split(',')
    if not parts:
        return line
    key = sentence_key(parts[0])
    if key in GN_POS_TYPES:
        lat_i, lat_h_i, lon_i, lon_h_i = GN_POS_TYPES[key]
        if len(parts) <= max(lat_i,lat_h_i,lon_i,lon_h_i):
            return line
        lat = dm_to_deg_safe(parts[lat_i], parts[lat_h_i])
        lon = dm_to_deg_safe(parts[lon_i], parts[lon_h_i])
        if lat is None or lon is None:
            if last_valid.get('lat') is None:
                return line
            new_lat, new_lon = last_valid['lat'], last_valid['lon']
        else:
            new_lat, new_lon = geodesic_direct(lat, lon, az_deg, distance_m)
            last_valid['lat'], last_valid['lon'] = new_lat, new_lon
        lat_field, lat_h = deg_to_dm_nmea(new_lat, True)
        lon_field, lon_h = deg_to_dm_nmea(new_lon, False)
        parts[lat_i], parts[lat_h_i], parts[lon_i], parts[lon_h_i] = lat_field, lat_h, lon_field, lon_h
        body = ",".join(parts)
        return f"${body}{nmea_checksum(body)}\r\n"
    return line

# -------------------- Main --------------------
def compute_shift_and_apply(infile: str, outfile: str, house: tuple, village_center: tuple, preview: int=0):
    dist_m, az = geodesic_inverse(house[0], house[1], village_center[0], village_center[1])
    last_valid = {'lat': None, 'lon': None}
    changed = 0
    printed = 0
    with open(infile,'r',encoding='utf-8',errors='replace') as fin, \
         open(outfile,'w',encoding='utf-8',errors='replace') as fout:
        for line in fin:
            new_line = process_line(line, dist_m, az, last_valid)
            if new_line != line:
                changed += 1
                if preview and printed < preview:
                    sys.stdout.write("OLD: "+line.rstrip("\n")+"\n")
                    sys.stdout.write("NEW: "+new_line.rstrip("\r\n")+"\n\n")
                    printed += 1
            fout.write(new_line)
    return dist_m, az, changed

def parse_args():
    p = argparse.ArgumentParser(description="Obfuscate NMEA positions by shifting each fix from house -> village center.")
    p.add_argument('infile')
    p.add_argument('outfile')
    p.add_argument('--house', nargs=2, type=float, metavar=('LAT','LON'), required=True,
                   help='House coordinates (required)')
    p.add_argument('--village-center', nargs=2, type=float, metavar=('LAT','LON'),
                   default=(44.148630208502965, -72.65679699901393),
                   help='Village center coordinates (optional default)')
    p.add_argument('--preview', type=int, default=0, help='Print first N changed lines')
    return p.parse_args()

if __name__ == "__main__":
    args = parse_args()
    house = (args.house[0], args.house[1])
    village_center = (args.village_center[0], args.village_center[1])
    dist, az, changed = compute_shift_and_apply(args.infile, args.outfile, house, village_center, preview=args.preview)
    print(f"Applied geodesic shift: {dist:.3f} m, azimuth {az:.6f}°. Modified {changed} lines.")

