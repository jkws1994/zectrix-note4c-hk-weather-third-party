"""Regenerate src/boundaries.inc from the official HAD JSON file (path in argv[1])."""
import json
import re
import sys
from pathlib import Path

project = Path(__file__).resolve().parents[1]
names = re.findall(r'^    \{"([^"]+)"', (project/"src/district.cpp").read_text(), re.M)
features = json.loads(Path(sys.argv[1]).read_text())["features"]
by_name = {f["properties"]["District"]: f["geometry"] for f in features}
assert len(names) == len(by_name) == 18
points, bounds = [], []
for name in names:
    geo = by_name[name + " District"]
    # The current official dataset supplies a single closed exterior ring per district.
    # Reject schema changes rather than silently discarding islands or interior rings.
    assert geo["type"] == "Polygon" and len(geo["coordinates"]) == 1
    ring = geo["coordinates"][0]
    assert len(ring) >= 4 and ring[0] == ring[-1]
    xy = [(round(lon*1e6), round(lat*1e6)) for lon, lat in ring[:-1]]
    x, y = zip(*xy)
    bounds.append((len(points), len(xy), min(x), max(x), min(y), max(y)))
    points.extend(xy)
assert len(points) < 65536
with (project/"src/boundaries.inc").open("w") as out:
    out.write("// Derived from Hong Kong Home Affairs Department district boundaries. See README.md.\n")
    out.write("static const Point points[] = {\n")
    for i in range(0,len(points),5):
        out.write("    " + ", ".join("{%d,%d}" % p for p in points[i:i+5]) + ",\n")
    out.write("};\nstatic const Ring rings[] = {\n")
    for name,b in zip(names,bounds):
        out.write("    {%d,%d,%d,%d,%d,%d}, // %s\n" % (*b,name))
    out.write("};\n")
