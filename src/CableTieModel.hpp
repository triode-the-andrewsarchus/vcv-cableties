#pragma once
#include <rack.hpp>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>

namespace CableTies {

using namespace rack;

/**
 * 2D Line segment intersection.
 * Checks if segment [p1, p2] intersects segment [p3, p4].
 */
inline bool getLineIntersection(math::Vec p1, math::Vec p2, math::Vec p3, math::Vec p4,
                                math::Vec* outIntersection = nullptr,
                                float* outT1 = nullptr,
                                float* outT2 = nullptr) {
	math::Vec d1 = p2.minus(p1);
	math::Vec d2 = p4.minus(p3);
	float cross = d1.x * d2.y - d1.y * d2.x;
	if (std::abs(cross) < 1e-6f)
		return false;

	math::Vec d3 = p3.minus(p1);
	float t1 = (d3.x * d2.y - d3.y * d2.x) / cross;
	float t2 = (d3.x * d1.y - d3.y * d1.x) / cross;

	if (t1 >= 0.f && t1 <= 1.f && t2 >= 0.f && t2 <= 1.f) {
		if (outIntersection) {
			*outIntersection = p1.plus(d1.mult(t1));
		}
		if (outT1) *outT1 = t1;
		if (outT2) *outT2 = t2;
		return true;
	}
	return false;
}

/** Distance from point p to line segment [a, b]. */
inline float distPointToSegment(math::Vec p, math::Vec a, math::Vec b) {
	math::Vec ab = b.minus(a);
	float l2 = ab.dot(ab);
	if (l2 < 1e-6f) return p.minus(a).norm();
	float t = std::max(0.f, std::min(1.f, p.minus(a).dot(ab) / l2));
	math::Vec proj = a.plus(ab.mult(t));
	return p.minus(proj).norm();
}

/** Cubic Bezier point evaluation */
inline math::Vec evalCubicBezier(math::Vec p0, math::Vec p1, math::Vec p2, math::Vec p3, float t) {
	float u = 1.f - t;
	float tt = t * t;
	float uu = u * u;
	float uuu = uu * u;
	float ttt = tt * t;

	math::Vec p = p0.mult(uuu);
	p = p.plus(p1.mult(3.f * uu * t));
	p = p.plus(p2.mult(3.f * u * tt));
	p = p.plus(p3.mult(ttt));
	return p;
}

/** Quadratic Bezier point evaluation */
inline math::Vec evalQuadBezier(math::Vec p0, math::Vec p1, math::Vec p2, float t) {
	float u = 1.f - t;
	return p0.mult(u * u).plus(p1.mult(2.f * u * t)).plus(p2.mult(t * t));
}

/**
 * Represents a single Cable Tie in the rack.
 */
struct CableTie {
	int id = 0;
	math::Vec pos;                       // Position in rack coordinates
	std::vector<int64_t> cableIds;       // IDs of cables passing through this tie
	NVGcolor color = nvgRGB(235, 238, 242); // Nylon white/gray band
	float width = 7.0f;                  // Thickness of band along cable
	float wireSpacing = 6.0f;            // Center-to-center spacing of wires

	// Calculated at render/update time
	math::Vec tangent = math::Vec(1.f, 0.f);
	math::Vec normal = math::Vec(0.f, 1.f);
	std::vector<int64_t> orderedCableIds; // Sorted across bundle width to avoid wire twists

	bool hasCable(int64_t cableId) const {
		for (int64_t cid : cableIds) {
			if (cid == cableId) return true;
		}
		return false;
	}

	void removeCable(int64_t cableId) {
		cableIds.erase(std::remove(cableIds.begin(), cableIds.end(), cableId), cableIds.end());
		orderedCableIds.erase(std::remove(orderedCableIds.begin(), orderedCableIds.end(), cableId), orderedCableIds.end());
	}
};

/**
 * Complete curve representation of a single cable segment.
 */
struct CableSegmentCurve {
	math::Vec p0, p1, p2, p3; // Cubic Bezier control points
	bool isQuad = false;
	math::Vec qControl;       // If quadratic: p0, qControl, p3
};

/**
 * Complete path of a cable (sequence of curve segments).
 */
struct CablePath {
	int64_t cableId = -1;
	app::CableWidget* cw = nullptr;
	std::vector<CableSegmentCurve> segments;
	std::vector<math::Vec> sampledPoints; // Flattened polyline for fast intersection
	std::vector<int> sampleSegmentIndices; // Which segment each sample came from
};

} // namespace CableTies
