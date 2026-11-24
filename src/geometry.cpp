#include "district.h"
#include "location.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
using Point = DistrictPoint;
struct Ring {
    uint16_t first, count;
    int32_t west, east, south, north;
};
#include "boundaries.inc"
static_assert(sizeof(rings)/sizeof(rings[0]) == districtCount, "District table mismatch");

bool containsCircle(const Ring& ring, Point p, float radiusSquared) {
    bool inside=false;
    Point a=points[ring.first+ring.count-1];
    for (unsigned i=0; i<ring.count; ++i) {
        Point b=points[ring.first+i];
        // Conservative metres per microdegree throughout Hong Kong. Subtract in integer space first.
        float ax=(a.x-p.x)*0.102f, ay=(a.y-p.y)*0.110f;
        float bx=(b.x-p.x)*0.102f, by=(b.y-p.y)*0.110f;
        float dx=bx-ax, dy=by-ay, lengthSquared=dx*dx+dy*dy;
        float t=lengthSquared ? std::max(0.f,std::min(1.f,-(ax*dx+ay*dy)/lengthSquared)) : 0.f;
        float x=ax+t*dx, y=ay+t*dy;
        if (x*x+y*y <= radiusSquared) return false;
        if ((ay>0)!=(by>0) && ax-ay*dx/dy>0) inside=!inside;
        a=b;
    }
    return inside;
}
}

DistrictBoundary districtBoundary(int index) {
    const Ring& ring=rings[index];
    return {points+ring.first,ring.count};
}

int districtAt(double latitude,double longitude,double accuracyMetres) {
    if (!(latitude>=22 && latitude<=23 && longitude>=113 && longitude<=115 &&
          accuracyMetres>0 && accuracyMetres<=500)) return -1;
    Point p{int32_t(std::lround(longitude*1e6)),int32_t(std::lround(latitude*1e6))};
    const float radius=float(accuracyMetres)+2.f; // Covers coordinate rounding at the edge.
    for (int i=0; i<districtCount; ++i) {
        const Ring& ring=rings[i];
        if (p.x>=ring.west && p.x<=ring.east && p.y>=ring.south && p.y<=ring.north &&
            containsCircle(ring,p,radius*radius)) return i;
    }
    return -1;
}
