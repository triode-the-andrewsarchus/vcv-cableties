#include "HarnessOverlayWidget.hpp"
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <settings.hpp>
#include <GLFW/glfw3.h>
#include <cmath>
#include <algorithm>

namespace ZipTies {

static HarnessOverlayWidget* gSharedOverlay = nullptr;

struct Waypoint {
	rack::math::Vec pos;
	rack::math::Vec tangent;
	bool isPort = false;
	Waypoint(rack::math::Vec p, rack::math::Vec t, bool port = false) : pos(p), tangent(t), isPort(port) {}
};

HarnessOverlayWidget* HarnessOverlayWidget::acquire(app::ModuleWidget* mw) {
	if (!mw || !APP || !APP->scene || !APP->scene->rack) return nullptr;

	if (!gSharedOverlay) {
		gSharedOverlay = new HarnessOverlayWidget;
		APP->scene->rack->addChild(gSharedOverlay);

		// Position right after cableContainer so it renders on top of cables
		std::list<widget::Widget*>::iterator it;
		for (it = APP->scene->rack->children.begin(); it != APP->scene->rack->children.end(); ++it) {
			if (*it == APP->scene->rack->getCableContainer()) break;
		}
		if (it != APP->scene->rack->children.end()) {
			++it;
			APP->scene->rack->children.splice(it, APP->scene->rack->children, --APP->scene->rack->children.end());
		}
	}

	gSharedOverlay->registeredWidgets.insert(mw);
	if (ZipTieModule* zm = dynamic_cast<ZipTieModule*>(mw->module)) {
		gSharedOverlay->module = zm;
		gSharedOverlay->ownerMw = mw;
	}
	return gSharedOverlay;
}

void HarnessOverlayWidget::release(app::ModuleWidget* mw) {
	if (!gSharedOverlay) return;
	gSharedOverlay->registeredWidgets.erase(mw);

	if (gSharedOverlay->ownerMw == mw) {
		gSharedOverlay->ownerMw = nullptr;
		gSharedOverlay->module = nullptr;
		// Check if another ZipTieModule is registered
		for (app::ModuleWidget* other : gSharedOverlay->registeredWidgets) {
			if (other && other->module) {
				if (ZipTieModule* zm = dynamic_cast<ZipTieModule*>(other->module)) {
					gSharedOverlay->module = zm;
					gSharedOverlay->ownerMw = other;
					break;
				}
			}
		}
	}

	if (gSharedOverlay->registeredWidgets.empty()) {
		// Restore any deflected untied cables before destroying overlay
		if (APP && APP->scene && APP->scene->rack) {
			for (int64_t cid : gSharedOverlay->deflectedUntiedCableIds) {
				if (app::CableWidget* cw = APP->scene->rack->getCable(cid)) {
					cw->visible = true;
					restoreCablePlugs(cw);
				}
			}
			APP->scene->rack->removeChild(gSharedOverlay);
		}
		delete gSharedOverlay;
		gSharedOverlay = nullptr;
	}
}

HarnessOverlayWidget::HarnessOverlayWidget() {
	box.pos = math::Vec(0, 0);
	if (APP && APP->scene && APP->scene->rack) {
		box.size = APP->scene->rack->box.size;
	}
}

HarnessOverlayWidget::~HarnessOverlayWidget() {
	if (gSharedOverlay == this) {
		gSharedOverlay = nullptr;
	}
}

bool HarnessOverlayWidget::isShortcutActive(int mods) const {
	if (!module) return false;
	if (module->toolArmed) return true;

	if (!APP || !APP->window || !APP->window->win) return false;
	GLFWwindow* win = APP->window->win;

	auto isKeyDown = [&](int key) -> bool {
		if (key <= 0) return false;
		if (glfwGetKey(win, key) == GLFW_PRESS) return true;
		if (APP->event && APP->event->heldKeys.count(key) > 0) return true;
		return false;
	};

	int currentMods = mods & RACK_MOD_MASK;
	if (isKeyDown(GLFW_KEY_LEFT_SHIFT) || isKeyDown(GLFW_KEY_RIGHT_SHIFT))
		currentMods |= GLFW_MOD_SHIFT;
	if (isKeyDown(GLFW_KEY_LEFT_CONTROL) || isKeyDown(GLFW_KEY_RIGHT_CONTROL))
		currentMods |= GLFW_MOD_CONTROL;
	if (isKeyDown(GLFW_KEY_LEFT_ALT) || isKeyDown(GLFW_KEY_RIGHT_ALT))
		currentMods |= GLFW_MOD_ALT;
	if (isKeyDown(GLFW_KEY_LEFT_SUPER) || isKeyDown(GLFW_KEY_RIGHT_SUPER))
		currentMods |= GLFW_MOD_SUPER;

	int reqKey = module->shortcutKey;
	int reqMods = module->shortcutMods & RACK_MOD_MASK;

	if (reqKey == 0 && reqMods == 0)
		return false;

	if ((currentMods & reqMods) != reqMods)
		return false;

	if (reqKey > 0 && !isModifierKey(reqKey)) {
		return isKeyDown(reqKey);
	}

	return true;
}

bool HarnessOverlayWidget::handleKeyLearn(int key, int action, int mods) {
	if (!module || !module->learningKey) return false;

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
		module->learningKey = false;
		module->pendingMods = 0;
		return true;
	}

	if (action == GLFW_PRESS) {
		if (isModifierKey(key)) {
			module->pendingMods |= modifierBitForKey(key) | (mods & RACK_MOD_MASK);
			return true;
		}
		else if (key > 0) {
			int finalMods = (mods & RACK_MOD_MASK) | module->pendingMods;
			module->setShortcut(key, finalMods);
			return true;
		}
	}
	else if (action == GLFW_RELEASE) {
		if (isModifierKey(key) && module->pendingMods != 0) {
			module->setShortcut(0, module->pendingMods);
			return true;
		}
	}
	return false;
}

void HarnessOverlayWidget::onHoverKey(const HoverKeyEvent& e) {
	if (handleKeyLearn(e.key, e.action, e.mods)) {
		e.consume(this);
		return;
	}
	TransparentWidget::onHoverKey(e);
}

void HarnessOverlayWidget::onSelectKey(const SelectKeyEvent& e) {
	if (handleKeyLearn(e.key, e.action, e.mods)) {
		e.consume(this);
		return;
	}
	TransparentWidget::onSelectKey(e);
}

// ============================================================================
// KEEP-OUT OBSTACLE & GEOMETRY HELPERS
// ============================================================================
void HarnessOverlayWidget::buildKeepOutObstacles() {
	cachedObstacles.clear();
	cachedActivePegs.clear();

	if (!APP || !APP->scene || !APP->scene->rack) return;

	// Collect all active PegWidgets on the rack
	std::vector<PegWidget*> pegWidgets;
	for (app::ModuleWidget* mw : registeredWidgets) {
		if (PegWidget* pw = dynamic_cast<PegWidget*>(mw)) {
			if (pw->module) {
				pegWidgets.push_back(pw);
			}
		}
	}
	if (pegWidgets.empty()) return;

	std::map<int64_t, PegWidget*> idToWidget;
	for (PegWidget* pw : pegWidgets) {
		idToWidget[pw->module->id] = pw;
		PegModule* pm = dynamic_cast<PegModule*>(pw->module);
		if (!pm) continue;

		// Record active (non-removed) pegs for 3D cap overlay rendering
		if (pm->linkedAboveId == -1) {
			cachedActivePegs.push_back({pw->box.pos.plus(math::Vec(PegModule::PEG_X, PegModule::TOP_PEG_Y))});
		}
		if (pm->linkedBelowId == -1) {
			cachedActivePegs.push_back({pw->box.pos.plus(math::Vec(PegModule::PEG_X, PegModule::BOT_PEG_Y))});
		}
	}

	// Step 1: Group vertically linked PegWidgets into Vertical Columns
	struct PegColumn {
		float x = 0.f;
		float yTop = 0.f;
		float yBot = 0.f;
		std::vector<int64_t> memberIds;
	};

	std::vector<PegColumn> columns;
	std::unordered_set<int64_t> visitedVert;
	std::map<int64_t, int> moduleToColIdx;

	for (PegWidget* pw : pegWidgets) {
		int64_t mid = pw->module->id;
		if (visitedVert.count(mid)) continue;

		// Walk up to top-most vertically linked PegWidget
		PegWidget* topW = pw;
		int guard = 0;
		while (guard++ < 32) {
			PegModule* tm = dynamic_cast<PegModule*>(topW->module);
			if (!tm || tm->linkedAboveId == -1) break;
			auto it = idToWidget.find(tm->linkedAboveId);
			if (it == idToWidget.end() || it->second == topW) break;
			topW = it->second;
		}

		// Walk down from topW to bottom-most vertically linked PegWidget
		PegColumn col;
		col.x = topW->box.pos.x + PegModule::PEG_X;
		col.yTop = topW->box.pos.y + PegModule::TOP_PEG_Y;
		PegWidget* curW = topW;
		guard = 0;
		while (curW && guard++ < 32) {
			int64_t cid = curW->module->id;
			if (visitedVert.count(cid)) break;
			visitedVert.insert(cid);
			col.memberIds.push_back(cid);
			col.yBot = curW->box.pos.y + PegModule::BOT_PEG_Y;

			PegModule* cm = dynamic_cast<PegModule*>(curW->module);
			if (!cm || cm->linkedBelowId == -1) break;
			auto it = idToWidget.find(cm->linkedBelowId);
			if (it == idToWidget.end()) break;
			curW = it->second;
		}

		int colIdx = (int)columns.size();
		for (int64_t memberId : col.memberIds) {
			moduleToColIdx[memberId] = colIdx;
		}
		columns.push_back(col);
	}

	// Step 2: Check horizontal links between columns & build KeepOutObstacles
	const float PEG_CLEARANCE = 11.0f;
	std::set<std::pair<int, int>> linkedColPairs;
	std::unordered_set<int> horizontallyLinkedCols;

	for (PegWidget* pw : pegWidgets) {
		PegModule* pm = dynamic_cast<PegModule*>(pw->module);
		if (!pm) continue;
		int cA = moduleToColIdx[pm->id];

		auto addHorizPair = [&](int64_t otherId) {
			if (otherId == -1) return;
			auto it = moduleToColIdx.find(otherId);
			if (it == moduleToColIdx.end()) return;
			int cB = it->second;
			if (cA == cB) return;
			int cMin = std::min(cA, cB);
			int cMax = std::max(cA, cB);
			if (linkedColPairs.insert({cMin, cMax}).second) {
				horizontallyLinkedCols.insert(cA);
				horizontallyLinkedCols.insert(cB);

				const PegColumn& col1 = columns[cMin];
				const PegColumn& col2 = columns[cMax];
				float leftX = std::min(col1.x, col2.x);
				float rightX = std::max(col1.x, col2.x);
				float topY = std::min(col1.yTop, col2.yTop);
				float botY = std::max(col1.yBot, col2.yBot);

				KeepOutObstacle obs;
				obs.isBox = true;
				obs.pegLeftX = leftX;
				obs.pegRightX = rightX;
				obs.pegTopY = topY;
				obs.pegBotY = botY;
				obs.xMin = leftX - PEG_CLEARANCE;
				obs.xMax = rightX + PEG_CLEARANCE;
				obs.yTop = topY - PEG_CLEARANCE;
				obs.yBot = botY + PEG_CLEARANCE;
				cachedObstacles.push_back(obs);
			}
		};

		addHorizPair(pm->linkedLeftId);
		addHorizPair(pm->linkedRightId);
	}

		// Standalone columns OR columns that are horizontally linked still also act as 1D Vertical Barriers
		// (so even if a port is inside a horizontal keep-out box, wires outside don't cross through the physical peg columns)
		for (size_t i = 0; i < columns.size(); ++i) {
			if (horizontallyLinkedCols.count((int)i)) continue;
			const PegColumn& col = columns[i];
			KeepOutObstacle obs;
			obs.isBox = false;
			obs.pegLeftX = col.x;
			obs.pegRightX = col.x;
			obs.pegTopY = col.yTop;
			obs.pegBotY = col.yBot;
			obs.xMin = col.x - PEG_CLEARANCE;
			obs.xMax = col.x + PEG_CLEARANCE;
			obs.yTop = col.yTop - PEG_CLEARANCE;
			obs.yBot = col.yBot + PEG_CLEARANCE;
			cachedObstacles.push_back(obs);
		}
	}

static bool pointInsideObstacle(math::Vec p, const KeepOutObstacle& obs, float pad = -2.0f) {
	if (obs.isBox) {
		return p.x > (obs.xMin - pad) && p.x < (obs.xMax + pad) &&
		       p.y > (obs.yTop - pad) && p.y < (obs.yBot + pad);
	}
	else {
		// 1D vertical barrier has zero interior area (unless a point is literally inside the 2HP peg column)
		return std::fabs(p.x - obs.pegLeftX) < 5.0f &&
		       p.y > (obs.pegTopY - 4.0f) && p.y < (obs.pegBotY + 4.0f);
	}
}

static bool segmentHitsObstacle(math::Vec a, math::Vec b, const KeepOutObstacle& obs, float shrink = 0.0f) {
	float xMin = obs.xMin + shrink;
	float xMax = obs.xMax - shrink;
	float yTop = obs.yTop + shrink;
	float yBot = obs.yBot - shrink;

	if (!obs.isBox) {
		// 1D vertical barrier segment at x = obs.pegLeftX, y in [pegTopY, pegBotY]
		// Notice: detour waypoints are placed at (pegLeftX, yTop) and (pegLeftX, yBot),
		// which are outside [pegTopY, pegBotY] by PEG_CLEARANCE (11px), so detour segments
		// touching the detour waypoints NEVER intersect the barrier segment [pegTopY, pegBotY]!
		float barTop = obs.pegTopY + shrink;
		float barBot = obs.pegBotY - shrink;
		if (barBot <= barTop) return false;

		// Ignore if an endpoint is already one of the detour waypoints for this barrier
		if (std::fabs(a.x - obs.pegLeftX) < 0.5f && (std::fabs(a.y - obs.yTop) < 1.5f || std::fabs(a.y - obs.yBot) < 1.5f)) {
			return false;
		}
		if (std::fabs(b.x - obs.pegLeftX) < 0.5f && (std::fabs(b.y - obs.yTop) < 1.5f || std::fabs(b.y - obs.yBot) < 1.5f)) {
			return false;
		}

		math::Vec p3(obs.pegLeftX, barTop);
		math::Vec p4(obs.pegLeftX, barBot);
		if (getLineIntersection(a, b, p3, p4)) return true;

		// Also check if segment passes through the physical top or bottom peg post (radius ~ 7.5px)
		float pegR = std::max(2.0f, 7.5f - shrink);
		if (distPointToSegment(math::Vec(obs.pegLeftX, obs.pegTopY), a, b) < pegR) return true;
		if (distPointToSegment(math::Vec(obs.pegLeftX, obs.pegBotY), a, b) < pegR) return true;
		return false;
	}

	// 2D Keep-Out Box [xMin, xMax] x [yTop, yBot]
	if (xMax <= xMin || yBot <= yTop) return false;

	// Trivial reject
	if (std::max(a.x, b.x) < xMin || std::min(a.x, b.x) > xMax ||
	    std::max(a.y, b.y) < yTop || std::min(a.y, b.y) > yBot) {
		return false;
	}

	// Check if midpoint or quarter-points are strictly inside the box interior
	for (float f : {0.25f, 0.5f, 0.75f}) {
		math::Vec pt = a.plus(b.minus(a).mult(f));
		if (pt.x > xMin + 0.5f && pt.x < xMax - 0.5f && pt.y > yTop + 0.5f && pt.y < yBot - 0.5f) return true;
	}

	// Shrink box slightly when testing edge intersections so segments running along the outer corners don't self-hit
	float exMin = xMin + 0.8f;
	float exMax = xMax - 0.8f;
	float eyTop = yTop + 0.8f;
	float eyBot = yBot - 0.8f;
	if (exMax <= exMin || eyBot <= eyTop) return false;

	math::Vec tl(exMin, eyTop), tr(exMax, eyTop), br(exMax, eyBot), bl(exMin, eyBot);
	if (getLineIntersection(a, b, tl, tr)) return true;
	if (getLineIntersection(a, b, tr, br)) return true;
	if (getLineIntersection(a, b, br, bl)) return true;
	if (getLineIntersection(a, b, bl, tl)) return true;

	return false;
}

static bool curveHitsObstacle(math::Vec p0, math::Vec p1, math::Vec p2, math::Vec p3, bool isQuad, const KeepOutObstacle& obs, float shrink = 0.0f) {
	const int N = 16;
	math::Vec prev = p0;
	for (int i = 1; i <= N; ++i) {
		float t = (float)i / (float)N;
		math::Vec cur = isQuad ? evalQuadBezier(p0, p1, p3, t) : evalCubicBezier(p0, p1, p2, p3, t);
		if (segmentHitsObstacle(prev, cur, obs, shrink)) return true;
		prev = cur;
	}
	return false;
}

// Compute detour waypoints around a single KeepOutObstacle for segment A -> B
static std::vector<math::Vec> computeObstacleDetour(math::Vec A, math::Vec B, const KeepOutObstacle& obs, float extraMargin = 0.0f) {
	std::vector<math::Vec> result;

	if (!obs.isBox) {
		// 1D Vertical Barrier at x = obs.pegLeftX, y in [obs.yTop, obs.yBot]
		float x = obs.pegLeftX;
		math::Vec topW(x, obs.yTop - extraMargin);
		math::Vec botW(x, obs.yBot + extraMargin);

		float costTop = A.minus(topW).norm() + topW.minus(B).norm();
		float costBot = A.minus(botW).norm() + botW.minus(B).norm();
		result.push_back((costTop <= costBot) ? topW : botW);
		return result;
	}

	// 2D Keep-Out Box: consider candidate 1-corner and 2-corner paths around the 4 corners of the box
	math::Vec corners[4] = {
		math::Vec(obs.xMin - extraMargin, obs.yTop - extraMargin), // 0: TL
		math::Vec(obs.xMax + extraMargin, obs.yTop - extraMargin), // 1: TR
		math::Vec(obs.xMax + extraMargin, obs.yBot + extraMargin), // 2: BR
		math::Vec(obs.xMin - extraMargin, obs.yBot + extraMargin)  // 3: BL
	};

	float bestCost = 1e18f;

	// Test 1-corner detours
	for (int c = 0; c < 4; ++c) {
		math::Vec C = corners[c];
		if (!segmentHitsObstacle(A, C, obs, 1.5f) && !segmentHitsObstacle(C, B, obs, 1.5f)) {
			float cost = A.minus(C).norm() + C.minus(B).norm();
			if (cost < bestCost) {
				bestCost = cost;
				result = {C};
			}
		}
	}

	// Test 2-corner detours along perimeter edges (adjacent corners)
	int adjPairs[8][2] = {
		{0, 1}, {1, 0}, // Top edge (TL <-> TR)
		{3, 2}, {2, 3}, // Bottom edge (BL <-> BR)
		{0, 3}, {3, 0}, // Left edge (TL <-> BL)
		{1, 2}, {2, 1}  // Right edge (TR <-> BR)
	};
	for (int p = 0; p < 8; ++p) {
		math::Vec C1 = corners[adjPairs[p][0]];
		math::Vec C2 = corners[adjPairs[p][1]];
		if (!segmentHitsObstacle(A, C1, obs, 1.5f) && !segmentHitsObstacle(C2, B, obs, 1.5f)) {
			float cost = A.minus(C1).norm() + C1.minus(C2).norm() + C2.minus(B).norm();
			if (cost < bestCost) {
				bestCost = cost;
				result = {C1, C2};
			}
		}
	}

	// Fallback if neither 1-corner nor 2-corner passed strict check: pick shortest top or bottom 2-corner bridge
	if (result.empty()) {
		bool leftToRight = (A.x <= B.x);
		std::vector<math::Vec> topBridge = leftToRight
			? std::vector<math::Vec>{corners[0], corners[1]}
			: std::vector<math::Vec>{corners[1], corners[0]};
		std::vector<math::Vec> botBridge = leftToRight
			? std::vector<math::Vec>{corners[3], corners[2]}
			: std::vector<math::Vec>{corners[2], corners[3]};

		float costTop = A.minus(topBridge[0]).norm() + topBridge[0].minus(topBridge[1]).norm() + topBridge[1].minus(B).norm();
		float costBot = A.minus(botBridge[0]).norm() + botBridge[0].minus(botBridge[1]).norm() + botBridge[1].minus(B).norm();
		result = (costTop <= costBot) ? topBridge : botBridge;
	}

	return result;
}

// Route a segment (from startPos to endPos) around all cachedObstacles.
// Returns detour points to insert between startPos and endPos.
// If the area is un-routable (endpoint inside a 2D keep-out box, or trapped inside conflicting 2D boxes), returns empty vector (routes straight through!).
static std::vector<math::Vec> routeSegmentAroundObstacles(
	math::Vec startPos,
	math::Vec endPos,
	bool useQuadSlump,
	const std::vector<KeepOutObstacle>& obstacles)
{
	if (obstacles.empty()) return {};

	// Filter out any obstacles that contain startPos or endPos (if a port/tie is inside a keep-out region, ignore that obstacle for this span)
	std::vector<const KeepOutObstacle*> validObstacles;
	validObstacles.reserve(obstacles.size());
	for (const KeepOutObstacle& obs : obstacles) {
		if (!pointInsideObstacle(startPos, obs, -2.0f) && !pointInsideObstacle(endPos, obs, -2.0f)) {
			validObstacles.push_back(&obs);
		}
	}
	if (validObstacles.empty()) return {};

	std::vector<math::Vec> pts = {startPos, endPos};
	const int MAX_ITERS = 8;
	bool anyInserted = false;
	std::unordered_set<const KeepOutObstacle*> handledObstacles;

	for (int iter = 0; iter < MAX_ITERS; ++iter) {
		bool changed = false;
		for (size_t s = 0; s + 1 < pts.size(); ++s) {
			math::Vec A = pts[s];
			math::Vec B = pts[s + 1];

			// Find first unhandled obstacle hit by segment A -> B (including cable sag if first pass)
			const KeepOutObstacle* hitObs = nullptr;
			float bestDist = 1e18f;

			for (const KeepOutObstacle* obs : validObstacles) {
				if (handledObstacles.count(obs)) continue;
				bool hit = false;
				if (!anyInserted && useQuadSlump) {
					float dist = A.minus(B).norm();
					math::Vec slump = A.plus(B).div(2.f);
					slump.y += (1.0f - settings::cableTension) * (150.0f + 1.0f * dist);
					hit = curveHitsObstacle(A, slump, slump, B, true, *obs, 0.0f);
				}
				else if (!anyInserted) {
					float dist = A.minus(B).norm();
					float sag = (1.0f - settings::cableTension) * (16.0f + 0.10f * dist);
					math::Vec dir = B.minus(A).normalize();
					math::Vec p1 = A.plus(dir.mult(dist * 0.35f)).plus(math::Vec(0.f, sag));
					math::Vec p2 = B.minus(dir.mult(dist * 0.35f)).plus(math::Vec(0.f, sag));
					hit = curveHitsObstacle(A, p1, p2, B, false, *obs, 0.0f);
				}
				else {
					hit = segmentHitsObstacle(A, B, *obs, 0.0f);
				}

				if (hit) {
					math::Vec obsCenter((obs->xMin + obs->xMax) * 0.5f, (obs->yTop + obs->yBot) * 0.5f);
					float d = A.minus(obsCenter).norm();
					if (d < bestDist) {
						bestDist = d;
						hitObs = obs;
					}
				}
			}

			if (hitObs) {
				handledObstacles.insert(hitObs);
				std::vector<math::Vec> detour = computeObstacleDetour(A, B, *hitObs);
				if (!detour.empty()) {
					pts.insert(pts.begin() + (s + 1), detour.begin(), detour.end());
					anyInserted = true;
					changed = true;
					break;
				}
			}
		}
		if (!changed) break;
	}

	if (!anyInserted) return {};

	// Un-routable check: Only fall back if a detour waypoint itself lands inside the interior of another 2D Keep-Out Box
	// (meaning two 2D keep-out boxes overlap/trap the corner so there is nowhere outside to place the wire)
	for (size_t i = 1; i + 1 < pts.size(); ++i) {
		for (const KeepOutObstacle* obs : validObstacles) {
			if (obs->isBox && pointInsideObstacle(pts[i], *obs, -5.0f)) {
				return {};
			}
		}
	}

	// Return interior detour points
	return std::vector<math::Vec>(pts.begin() + 1, pts.end() - 1);
}

void HarnessOverlayWidget::step() {
	// Process any pending clear request signaled lock-free by the audio thread or reset
	if (module && module->clearRequested.exchange(false, std::memory_order_relaxed)) {
		module->clearAllZipTies();
	}

	// Keep overlay matched to rack dimensions
	if (APP && APP->scene && APP->scene->rack) {
		box.pos = math::Vec(0, 0);
		box.size = APP->scene->rack->box.size;
	}

	// Build Keep-Out Obstacles from all PegWidgets on the rack
	buildKeepOutObstacles();

	// Build O(1) lookup map of complete cables once per UI step
	std::unordered_map<int64_t, app::CableWidget*> activeCables;
	if (APP && APP->scene && APP->scene->rack) {
		std::vector<app::CableWidget*> completeCables = APP->scene->rack->getCompleteCables();
		activeCables.reserve(completeCables.size());
		for (app::CableWidget* cw : completeCables) {
			if (!cw || !cw->cable) continue;
			int64_t cid = cw->cable->id;
			activeCables[cid] = cw;
		}
	}

	// Clean any unplugged/deleted cables without locking the audio engine
	if (module) {
		module->cleanDeadCables(activeCables);
	}

	// Update cached geometry for tied and peg-deflected cables
	updateCablePaths(activeCables);

	// If cutting, update current mouse position and intersections
	if (isCutting && APP && APP->scene && APP->scene->rack) {
		cutCurrentPos = APP->scene->rack->getMousePos();
		updateCutIntersections();
	}

	// Hit test zip ties for hover state
	hoveredTieId = -1;
	if (module && APP && APP->scene && APP->scene->rack) {
		math::Vec mouse = APP->scene->rack->getMousePos();
		for (const ZipTie& z : module->zipTies) {
			if (mouse.minus(z.pos).norm() <= 16.0f) {
				hoveredTieId = z.id;
				break;
			}
		}
	}

	TransparentWidget::step();
}

void HarnessOverlayWidget::updateCablePaths(const std::unordered_map<int64_t, app::CableWidget*>& activeCables) {
	cachedTiedPaths.clear();
	cachedUntiedPaths.clear();
	if (!APP || !APP->scene || !APP->scene->rack) return;

	auto findCable = [&](int64_t cid) -> app::CableWidget* {
		auto it = activeCables.find(cid);
		return (it != activeCables.end()) ? it->second : nullptr;
	};

	// Helper to build smooth cubic Bezier segments from a list of Waypoints and align jack plugs
	auto buildCubicSegmentsFromWaypoints = [&](CablePath& cp, app::CableWidget* cw, std::vector<Waypoint>& waypoints, bool sampleAlways) {
		for (size_t s = 0; s < waypoints.size() - 1; s++) {
			Waypoint& w0 = waypoints[s];
			Waypoint& w1 = waypoints[s + 1];

			math::Vec rawP0 = w0.pos;
			math::Vec rawP3 = w1.pos;

			float dist = rawP3.minus(rawP0).norm();
			math::Vec t0 = w0.tangent;
			math::Vec t1 = w1.tangent;

			if (w0.isPort) {
				t0 = rawP3.minus(rawP0).normalize().plus(math::Vec(0.f, 0.7f)).normalize();
			}
			else {
				if (t0.dot(rawP3.minus(rawP0)) < 0.f) t0 = t0.neg();
			}

			if (w1.isPort) {
				t1 = rawP3.minus(rawP0).normalize().plus(math::Vec(0.f, -0.7f)).normalize();
			}
			else {
				if (t1.dot(rawP3.minus(rawP0)) < 0.f) t1 = t1.neg();
			}

			math::Vec p1 = rawP0.plus(t0.mult(dist * 0.36f));
			math::Vec p2 = rawP3.minus(t1.mult(dist * 0.36f));

			// Tension / sag droop
			float sag = (1.0f - settings::cableTension) * (16.0f + 0.10f * dist);
			p1.y += sag;
			p2.y += sag;

			math::Vec p0 = rawP0;
			math::Vec p3 = rawP3;

			// Align output plug direction & start wire at exact tip of plug (14px from port center)
			if (w0.isPort) {
				math::Vec outVec = p1.minus(rawP0);
				float outLen = outVec.norm();
				math::Vec outDir = (outLen > 1e-3f) ? outVec.div(outLen) : math::Vec(0.f, 1.f);
				if (outLen < 18.0f) {
					p1 = rawP0.plus(outDir.mult(18.0f));
				}
				p0 = rawP0.plus(outDir.mult(14.0f));
				setPlugRotation(cw->outputPlug, outDir.arg());
			}

			// Align input plug direction & end wire at exact tip of plug (14px from port center)
			if (w1.isPort) {
				math::Vec inVec = p2.minus(rawP3);
				float inLen = inVec.norm();
				math::Vec inDir = (inLen > 1e-3f) ? inVec.div(inLen) : math::Vec(0.f, 1.f);
				if (inLen < 18.0f) {
					p2 = rawP3.plus(inDir.mult(18.0f));
				}
				p3 = rawP3.plus(inDir.mult(14.0f));
				setPlugRotation(cw->inputPlug, inDir.arg());
			}

			CableSegmentCurve curve;
			curve.p0 = p0;
			curve.p1 = p1;
			curve.p2 = p2;
			curve.p3 = p3;
			curve.isQuad = false;
			cp.segments.push_back(curve);

			if (sampleAlways || isCutting) {
				const int NUM_SAMPLES = 25;
				for (int k = 0; k <= NUM_SAMPLES; k++) {
					float t = (float)k / (float)NUM_SAMPLES;
					math::Vec pt = evalCubicBezier(p0, p1, p2, p3, t);
					cp.sampledPoints.push_back(pt);
					cp.sampleSegmentIndices.push_back((int)s);
				}
			}
		}
	};

	// ========================================================================
	// PART A: TIED CABLES (When ZipTieModule is present)
	// ========================================================================
	if (module) {
		std::lock_guard<std::recursive_mutex> lock(module->stateMutex);

		// STEP 1: Compute Tangent, Normal, and Ordered Wires for each Zip Tie
		for (ZipTie& z : module->zipTies) {
			math::Vec inVec(0.f, 0.f);
			math::Vec outVec(0.f, 0.f);
			int inCount = 0;
			int outCount = 0;

			for (int64_t cid : z.cableIds) {
				app::CableWidget* cw = findCable(cid);
				if (!cw || !cw->isComplete()) continue;

				auto it = module->cableTieSequences.find(cid);
				if (it == module->cableTieSequences.end()) continue;
				const auto& seq = it->second;

				auto tieIt = std::find(seq.begin(), seq.end(), z.id);
				if (tieIt == seq.end()) continue;
				int idx = (int)(tieIt - seq.begin());

				math::Vec prevPoint;
				if (idx == 0) {
					prevPoint = cw->getOutputPos();
				}
				else {
					const ZipTie* pz = module->getZipTie(seq[idx - 1]);
					prevPoint = pz ? pz->pos : cw->getOutputPos();
				}
				inVec = inVec.plus(z.pos.minus(prevPoint).normalize());
				inCount++;

				math::Vec nextPoint;
				if (idx == (int)seq.size() - 1) {
					nextPoint = cw->getInputPos();
				}
				else {
					const ZipTie* nz = module->getZipTie(seq[idx + 1]);
					nextPoint = nz ? nz->pos : cw->getInputPos();
				}
				outVec = outVec.plus(nextPoint.minus(z.pos).normalize());
				outCount++;
			}

			math::Vec avgTangent(1.f, 0.f);
			if (inCount > 0 && outCount > 0) {
				avgTangent = inVec.div((float)inCount).plus(outVec.div((float)outCount)).normalize();
			}
			else if (inCount > 0) {
				avgTangent = inVec.div((float)inCount).normalize();
			}
			else if (outCount > 0) {
				avgTangent = outVec.div((float)outCount).normalize();
			}

			if (avgTangent.norm() < 0.001f) {
				avgTangent = math::Vec(1.f, 0.f);
			}
			z.tangent = avgTangent;
			z.normal = math::Vec(-avgTangent.y, avgTangent.x);

			// Order cables across normal to keep parallel ribbon untwisted
			z.orderedCableIds = z.cableIds;
			std::sort(z.orderedCableIds.begin(), z.orderedCableIds.end(), [&](int64_t a, int64_t b) {
				app::CableWidget* cwa = findCable(a);
				app::CableWidget* cwb = findCable(b);
				if (!cwa || !cwb) return a < b;
				math::Vec pa = cwa->getOutputPos();
				math::Vec pb = cwb->getOutputPos();
				float proja = pa.minus(z.pos).dot(z.normal);
				float projb = pb.minus(z.pos).dot(z.normal);
				return proja < projb;
			});
		}

		// STEP 2: Compute Full Paths for Tied Cables (including Peg Deflection!)
		for (const auto& pair : module->cableTieSequences) {
			int64_t cid = pair.first;
			const auto& seq = pair.second;
			app::CableWidget* cw = findCable(cid);
			if (!cw || !cw->isComplete() || seq.empty()) continue;

			if (cw->visible) cw->visible = false;

			CablePath cp;
			cp.cableId = cid;
			cp.cw = cw;

			math::Vec outPos = cw->getOutputPos();
			math::Vec inPos = cw->getInputPos();

			// Collect base waypoints AND bundle centerline reference points (so tied bundles deflect together!)
			struct TiedNode {
				math::Vec wirePos;
				math::Vec bundleCenterPos;
				math::Vec tangent;
				float wireOffset;
				bool isPort;
				TiedNode(math::Vec wp, math::Vec bc, math::Vec tan, float off, bool port)
					: wirePos(wp), bundleCenterPos(bc), tangent(tan), wireOffset(off), isPort(port) {}
			};

			std::vector<TiedNode> nodes;
			nodes.reserve(seq.size() + 2);

			// Output Port node: compute average output port position of all cables sharing the first zip tie
			math::Vec firstBundleOut = outPos;
			float firstOffset = 0.f;
			if (const ZipTie* firstZ = module->getZipTie(seq.front())) {
				math::Vec sumOut(0.f, 0.f);
				int cntOut = 0;
				for (int64_t bcid : firstZ->cableIds) {
					if (app::CableWidget* bcw = findCable(bcid)) {
						sumOut = sumOut.plus(bcw->getOutputPos());
						cntOut++;
					}
				}
				if (cntOut > 0) firstBundleOut = sumOut.div((float)cntOut);
				auto it = std::find(firstZ->orderedCableIds.begin(), firstZ->orderedCableIds.end(), cid);
				int wIdx = (it != firstZ->orderedCableIds.end()) ? (int)(it - firstZ->orderedCableIds.begin()) : 0;
				firstOffset = (wIdx - ((int)firstZ->orderedCableIds.size() - 1) * 0.5f) * firstZ->wireSpacing;
			}
			nodes.push_back(TiedNode(outPos, firstBundleOut, math::Vec(0.f, 1.f), firstOffset, true));

			for (int tid : seq) {
				const ZipTie* z = module->getZipTie(tid);
				if (!z) continue;

				int wireIdx = 0;
				int totalWires = (int)z->orderedCableIds.size();
				auto it = std::find(z->orderedCableIds.begin(), z->orderedCableIds.end(), cid);
				if (it != z->orderedCableIds.end()) {
					wireIdx = (int)(it - z->orderedCableIds.begin());
				}

				float offset = (wireIdx - (totalWires - 1) * 0.5f) * z->wireSpacing;
				math::Vec wirePos = z->pos.plus(z->normal.mult(offset));
				nodes.push_back(TiedNode(wirePos, z->pos, z->tangent, offset, false));
			}

			// Input Port node: compute average input port position of all cables sharing the last zip tie
			math::Vec lastBundleIn = inPos;
			float lastOffset = 0.f;
			if (const ZipTie* lastZ = module->getZipTie(seq.back())) {
				math::Vec sumIn(0.f, 0.f);
				int cntIn = 0;
				for (int64_t bcid : lastZ->cableIds) {
					if (app::CableWidget* bcw = findCable(bcid)) {
						sumIn = sumIn.plus(bcw->getInputPos());
						cntIn++;
					}
				}
				if (cntIn > 0) lastBundleIn = sumIn.div((float)cntIn);
				auto it = std::find(lastZ->orderedCableIds.begin(), lastZ->orderedCableIds.end(), cid);
				int wIdx = (it != lastZ->orderedCableIds.end()) ? (int)(it - lastZ->orderedCableIds.begin()) : 0;
				lastOffset = (wIdx - ((int)lastZ->orderedCableIds.size() - 1) * 0.5f) * lastZ->wireSpacing;
			}
			nodes.push_back(TiedNode(inPos, lastBundleIn, math::Vec(0.f, -1.f), lastOffset, true));

			// Expand nodes with Peg Detour Waypoints when a span crosses Keep-Out Obstacles
			std::vector<Waypoint> finalWaypoints;
			for (size_t i = 0; i < nodes.size(); ++i) {
				finalWaypoints.push_back(Waypoint(nodes[i].wirePos, nodes[i].tangent, nodes[i].isPort));

				if (i + 1 < nodes.size() && !cachedObstacles.empty()) {
					// Route using the bundle centerline so all wires in a zip-tied group deflect together around the same peg!
					std::vector<math::Vec> detours = routeSegmentAroundObstacles(
						nodes[i].bundleCenterPos,
						nodes[i + 1].bundleCenterPos,
						false,
						cachedObstacles
					);

					if (!detours.empty()) {
						float avgOffset = 0.5f * (nodes[i].wireOffset + nodes[i + 1].wireOffset);
						for (size_t d = 0; d < detours.size(); ++d) {
							math::Vec prevP = (d == 0) ? nodes[i].bundleCenterPos : detours[d - 1];
							math::Vec nextP = (d + 1 == detours.size()) ? nodes[i + 1].bundleCenterPos : detours[d + 1];
							math::Vec dirIn = detours[d].minus(prevP).normalize();
							math::Vec dirOut = nextP.minus(detours[d]).normalize();
							math::Vec pegTan = dirIn.plus(dirOut).normalize();
							if (pegTan.norm() < 1e-3f) pegTan = dirOut;
							math::Vec pegNorm(-pegTan.y, pegTan.x);

							math::Vec deflectedWirePos = detours[d].plus(pegNorm.mult(avgOffset));
							finalWaypoints.push_back(Waypoint(deflectedWirePos, pegTan, false));
						}
					}
				}
			}

			buildCubicSegmentsFromWaypoints(cp, cw, finalWaypoints, false);
			cachedTiedPaths.push_back(cp);
		}
	}

	// ========================================================================
	// PART B: UNTIED CABLES (Deflected by Pegs OR Sampled while Slicing)
	// ========================================================================
	std::unordered_set<int64_t> newDeflectedUntiedIds;

	for (const auto& kv : activeCables) {
		int64_t cid = kv.first;
		app::CableWidget* cw = kv.second;
		if (!cw || !cw->cable) continue;
		if (module && module->isCableTied(cid)) continue;

		math::Vec outPos = cw->getOutputPos();
		math::Vec inPos = cw->getInputPos();

		std::vector<math::Vec> detours;
		if (!cachedObstacles.empty()) {
			detours = routeSegmentAroundObstacles(outPos, inPos, true, cachedObstacles);
		}

		if (!detours.empty()) {
			// This untied cable is deflected by one or more Peg modules!
			newDeflectedUntiedIds.insert(cid);
			if (cw->visible) cw->visible = false;

			CablePath cp;
			cp.cableId = cid;
			cp.cw = cw;

			std::vector<Waypoint> waypoints;
			waypoints.reserve(detours.size() + 2);
			waypoints.push_back(Waypoint(outPos, math::Vec(0.f, 1.f), true));
			for (size_t d = 0; d < detours.size(); ++d) {
				math::Vec prevP = (d == 0) ? outPos : detours[d - 1];
				math::Vec nextP = (d + 1 == detours.size()) ? inPos : detours[d + 1];
				math::Vec dirIn = detours[d].minus(prevP).normalize();
				math::Vec dirOut = nextP.minus(detours[d]).normalize();
				math::Vec pegTan = dirIn.plus(dirOut).normalize();
				if (pegTan.norm() < 1e-3f) pegTan = dirOut;
				waypoints.push_back(Waypoint(detours[d], pegTan, false));
			}
			waypoints.push_back(Waypoint(inPos, math::Vec(0.f, -1.f), true));

			buildCubicSegmentsFromWaypoints(cp, cw, waypoints, false);
			cachedTiedPaths.push_back(cp);
		}
		else {
			// Not deflected by pegs and not tied: ensure normal Rack cable is visible and plugs restored
			if (!cw->visible) {
				cw->visible = true;
			}
			if (deflectedUntiedCableIds.count(cid)) {
				restoreCablePlugs(cw);
			}

			// If user is actively slicing, sample the normal slumped cable for zip-tie cut intersections
			if (isCutting) {
				CablePath cp;
				cp.cableId = cid;
				cp.cw = cw;

				float dist = outPos.minus(inPos).norm();
				math::Vec slump = outPos.plus(inPos).div(2.f);
				slump.y += (1.0f - settings::cableTension) * (150.0f + 1.0f * dist);

				math::Vec cOut = outPos.plus(slump.minus(outPos).normalize().mult(14.f));
				math::Vec cIn = inPos.plus(slump.minus(inPos).normalize().mult(14.f));

				CableSegmentCurve curve;
				curve.p0 = cOut;
				curve.qControl = slump;
				curve.p3 = cIn;
				curve.isQuad = true;
				cp.segments.push_back(curve);

				const int NUM_SAMPLES = 30;
				cp.sampledPoints.reserve(NUM_SAMPLES + 1);
				cp.sampleSegmentIndices.reserve(NUM_SAMPLES + 1);
				for (int k = 0; k <= NUM_SAMPLES; k++) {
					float t = (float)k / (float)NUM_SAMPLES;
					math::Vec pt = evalQuadBezier(cOut, slump, cIn, t);
					cp.sampledPoints.push_back(pt);
					cp.sampleSegmentIndices.push_back(0);
				}

				cachedUntiedPaths.push_back(cp);
			}
		}
	}

	deflectedUntiedCableIds = std::move(newDeflectedUntiedIds);
}

void HarnessOverlayWidget::updateCutIntersections() {
	intersectedCables.clear();
	if (!isCutting) return;

	std::set<int64_t> seenCableIds;

	// Check tied (and peg-deflected) paths
	for (const CablePath& cp : cachedTiedPaths) {
		if (seenCableIds.count(cp.cableId)) continue;
		for (size_t i = 0; i + 1 < cp.sampledPoints.size(); i++) {
			math::Vec p3 = cp.sampledPoints[i];
			math::Vec p4 = cp.sampledPoints[i + 1];
			math::Vec hit;
			if (getLineIntersection(cutStartPos, cutCurrentPos, p3, p4, &hit)) {
				IntersectedCableInfo info;
				info.cableId = cp.cableId;
				info.cw = cp.cw;
				info.intersectionPoint = hit;
				info.segmentIndex = (module && module->isCableTied(cp.cableId)) ? cp.sampleSegmentIndices[i] : 0;
				intersectedCables.push_back(info);
				seenCableIds.insert(cp.cableId);
				break;
			}
		}
	}

	// Check untied paths
	for (const CablePath& cp : cachedUntiedPaths) {
		if (seenCableIds.count(cp.cableId)) continue;
		for (size_t i = 0; i + 1 < cp.sampledPoints.size(); i++) {
			math::Vec p3 = cp.sampledPoints[i];
			math::Vec p4 = cp.sampledPoints[i + 1];
			math::Vec hit;
			if (getLineIntersection(cutStartPos, cutCurrentPos, p3, p4, &hit)) {
				IntersectedCableInfo info;
				info.cableId = cp.cableId;
				info.cw = cp.cw;
				info.intersectionPoint = hit;
				info.segmentIndex = 0;
				intersectedCables.push_back(info);
				seenCableIds.insert(cp.cableId);
				break;
			}
		}
	}
}

void HarnessOverlayWidget::onButton(const ButtonEvent& e) {
	// Always allow clicks on any registered ZipTie or Peg module panel to pass through
	// so the user can toggle ARM TOOL, SNIP ALL, Vertical LINK buttons, or right-click menus at any time
	for (app::ModuleWidget* mw : registeredWidgets) {
		if (mw && mw->box.contains(e.pos)) {
			TransparentWidget::onButton(e);
			return;
		}
	}

	if (!module) {
		TransparentWidget::onButton(e);
		return;
	}

	// Left click handling
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		// 1. If shortcut is active, always draw tie cut line
		if (isShortcutActive(e.mods)) {
			isCutting = true;
			cutStartPos = e.pos;
			cutCurrentPos = e.pos;
			intersectedCables.clear();
			if (APP && APP->scene && APP->scene->rack) {
				std::unordered_map<int64_t, app::CableWidget*> activeCables;
				for (app::CableWidget* cw : APP->scene->rack->getCompleteCables()) {
					if (cw && cw->cable) activeCables[cw->cable->id] = cw;
				}
				updateCablePaths(activeCables);
			}
			e.consume(this);
			return;
		}

		// 2. If shortcut is not active, check if clicking on an existing zip tie to drag it
		for (const ZipTie& z : module->zipTies) {
			if (e.pos.minus(z.pos).norm() <= 16.0f) {
				draggedTieId = z.id;
				dragOffset = e.pos.minus(z.pos);
				e.consume(this);
				return;
			}
		}
	}

	// Right click handling: delete zip tie
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
		for (const ZipTie& z : module->zipTies) {
			if (e.pos.minus(z.pos).norm() <= 16.0f) {
				module->removeZipTie(z.id);
				hoveredTieId = -1;
				e.consume(this);
				return;
			}
		}
	}

	// Release handling
	if (e.action == GLFW_RELEASE && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		if (isCutting) {
			if (!intersectedCables.empty()) {
				math::Vec sum(0.f, 0.f);
				std::vector<int64_t> cableIds;
				std::map<int64_t, int> insertPositions;

				for (const auto& info : intersectedCables) {
					sum = sum.plus(info.intersectionPoint);
					cableIds.push_back(info.cableId);
					insertPositions[info.cableId] = info.segmentIndex;
				}

				math::Vec centroid = sum.div((float)intersectedCables.size());
				module->addZipTie(centroid, cableIds, insertPositions);
			}
			isCutting = false;
			intersectedCables.clear();
			e.consume(this);
			return;
		}

		if (draggedTieId != -1) {
			draggedTieId = -1;
			e.consume(this);
			return;
		}
	}

	TransparentWidget::onButton(e);
}

void HarnessOverlayWidget::onDragStart(const DragStartEvent& e) {
	if (isCutting || draggedTieId != -1) {
		e.consume(this);
		return;
	}
	TransparentWidget::onDragStart(e);
}

void HarnessOverlayWidget::onDragMove(const DragMoveEvent& e) {
	if (draggedTieId != -1 && module) {
		math::Vec newPos = APP->scene->rack->getMousePos().minus(dragOffset);
		module->moveZipTie(draggedTieId, newPos);
		e.consume(this);
		return;
	}

	if (isCutting) {
		cutCurrentPos = APP->scene->rack->getMousePos();
		updateCutIntersections();
		e.consume(this);
		return;
	}

	TransparentWidget::onDragMove(e);
}

void HarnessOverlayWidget::onDragEnd(const DragEndEvent& e) {
	if (isCutting) {
		if (!intersectedCables.empty() && module) {
			math::Vec sum(0.f, 0.f);
			std::vector<int64_t> cableIds;
			std::map<int64_t, int> insertPositions;

			for (const auto& info : intersectedCables) {
				sum = sum.plus(info.intersectionPoint);
				cableIds.push_back(info.cableId);
				insertPositions[info.cableId] = info.segmentIndex;
			}

			math::Vec centroid = sum.div((float)intersectedCables.size());
			module->addZipTie(centroid, cableIds, insertPositions);
		}
		isCutting = false;
		intersectedCables.clear();
		e.consume(this);
		return;
	}

	if (draggedTieId != -1) {
		draggedTieId = -1;
		e.consume(this);
		return;
	}

	TransparentWidget::onDragEnd(e);
}

void HarnessOverlayWidget::onHover(const HoverEvent& e) {
	TransparentWidget::onHover(e);
}

void HarnessOverlayWidget::drawLayer(const DrawArgs& args, int layer) {
	// Layer 3 is where RackWidget draws cables and plugs
	if (layer != 3) {
		TransparentWidget::drawLayer(args, layer);
		return;
	}

	NVGcontext* vg = args.vg;
	float opacity = settings::cableOpacity;
	if (opacity <= 0.0f && !isCutting) return;

	nvgSave(vg);
	nvgLineCap(vg, NVG_ROUND);
	nvgLineJoin(vg, NVG_ROUND);

	// -------------------------------------------------------------
	// 1. DRAW CABLE SHADOWS (Layer -1 equivalent)
	// -------------------------------------------------------------
	for (const CablePath& cp : cachedTiedPaths) {
		if (!cp.cw) continue;
		float cableOpacity = opacity;
		bool thick = false;

		if (cp.cw->cable && cp.cw->cable->outputModule) {
			engine::Output* out = &cp.cw->cable->outputModule->outputs[cp.cw->cable->outputId];
			thick = out->isPolyphonic();

			widget::Widget* hoveredWidget = APP->event ? APP->event->hoveredWidget : nullptr;
			if (cp.cw->outputPort == hoveredWidget || cp.cw->inputPort == hoveredWidget) {
				cableOpacity = 1.0f;
			}
			else if (out->getChannels() == 0) {
				cableOpacity *= 0.5f;
			}
		}

		if (cableOpacity <= 0.0f) continue;

		nvgSave(vg);
		nvgAlpha(vg, std::pow(cableOpacity, 1.5f));
		float thickness = thick ? 9.0f : 6.0f;
		NVGcolor shadowColor = nvgRGBAf(0.f, 0.f, 0.f, 0.10f);
		nvgStrokeColor(vg, shadowColor);
		nvgStrokeWidth(vg, thickness - 1.0f);

		nvgBeginPath(vg);
		for (size_t s = 0; s < cp.segments.size(); ++s) {
			const CableSegmentCurve& sc = cp.segments[s];
			math::Vec shP0 = (s == 0) ? sc.p0 : sc.p0.plus(math::Vec(0.f, 30.f));
			math::Vec shP1 = sc.p1.plus(math::Vec(0.f, 30.f));
			math::Vec shP2 = sc.p2.plus(math::Vec(0.f, 30.f));
			math::Vec shP3 = (s + 1 == cp.segments.size()) ? sc.p3 : sc.p3.plus(math::Vec(0.f, 30.f));

			nvgMoveTo(vg, shP0.x, shP0.y);
			nvgBezierTo(vg, shP1.x, shP1.y, shP2.x, shP2.y, shP3.x, shP3.y);
		}
		nvgStroke(vg);
		nvgRestore(vg);
	}

	// -------------------------------------------------------------
	// 2. DRAW CABLE BODIES (Outline + Inner)
	// -------------------------------------------------------------
	std::set<int64_t> highlightedCables;
	for (const auto& info : intersectedCables) {
		highlightedCables.insert(info.cableId);
	}

	// Also highlight untied cables that are currently intersected by the cut line
	for (const CablePath& cp : cachedUntiedPaths) {
		if (!cp.cw) continue;
		if (highlightedCables.count(cp.cableId) == 0) continue;

		bool thick = false;
		if (cp.cw->cable && cp.cw->cable->outputModule) {
			thick = cp.cw->cable->outputModule->outputs[cp.cw->cable->outputId].isPolyphonic();
		}
		float thickness = thick ? 9.0f : 6.0f;

		nvgSave(vg);
		nvgBeginPath(vg);
		for (const CableSegmentCurve& sc : cp.segments) {
			nvgMoveTo(vg, sc.p0.x, sc.p0.y);
			nvgQuadTo(vg, sc.qControl.x, sc.qControl.y, sc.p3.x, sc.p3.y);
		}
		nvgStrokeColor(vg, nvgRGBAf(0.4f, 0.8f, 1.0f, 0.65f));
		nvgStrokeWidth(vg, thickness + 4.0f);
		nvgStroke(vg);
		nvgRestore(vg);
	}

	for (const CablePath& cp : cachedTiedPaths) {
		if (!cp.cw) continue;

		NVGcolor cableColor = cp.cw->color;
		bool thick = false;
		float cableOpacity = opacity;

		if (cp.cw->cable && cp.cw->cable->outputModule) {
			engine::Output* out = &cp.cw->cable->outputModule->outputs[cp.cw->cable->outputId];
			thick = out->isPolyphonic();

			widget::Widget* hoveredWidget = APP->event ? APP->event->hoveredWidget : nullptr;
			if (cp.cw->outputPort == hoveredWidget || cp.cw->inputPort == hoveredWidget) {
				cableOpacity = 1.0f;
			}
			else if (out->getChannels() == 0) {
				cableOpacity *= 0.5f;
			}
		}

		bool isHighlighted = highlightedCables.count(cp.cableId) > 0;
		if (isHighlighted) cableOpacity = 1.0f;
		if (cableOpacity <= 0.0f) continue;

		nvgSave(vg);
		nvgAlpha(vg, std::pow(cableOpacity, 1.5f));
		float thickness = thick ? 9.0f : 6.0f;

		// Highlight outer glow if intersected by cut line
		if (isHighlighted) {
			nvgBeginPath(vg);
			for (const CableSegmentCurve& sc : cp.segments) {
				nvgMoveTo(vg, sc.p0.x, sc.p0.y);
				nvgBezierTo(vg, sc.p1.x, sc.p1.y, sc.p2.x, sc.p2.y, sc.p3.x, sc.p3.y);
			}
			nvgStrokeColor(vg, nvgRGBAf(0.4f, 0.8f, 1.0f, 0.65f));
			nvgStrokeWidth(vg, thickness + 4.0f);
			nvgStroke(vg);
		}

		// Cable Outline + Inner Body (matching VCV Rack CableWidget::drawLayer)
		nvgBeginPath(vg);
		for (const CableSegmentCurve& sc : cp.segments) {
			nvgMoveTo(vg, sc.p0.x, sc.p0.y);
			nvgBezierTo(vg, sc.p1.x, sc.p1.y, sc.p2.x, sc.p2.y, sc.p3.x, sc.p3.y);
		}
		nvgStrokeColor(vg, color::mult(cableColor, 0.8f));
		nvgStrokeWidth(vg, thickness);
		nvgStroke(vg);

		nvgStrokeColor(vg, color::mult(cableColor, 0.95f));
		nvgStrokeWidth(vg, thickness - 1.0f);
		nvgStroke(vg);

		nvgRestore(vg);
	}

	// -------------------------------------------------------------
	// 3. DRAW 3D TURRET PEG TOP CAPS (So cables tuck behind the peg cap)
	// -------------------------------------------------------------
	for (const ActivePegCap& peg : cachedActivePegs) {
		nvgSave(vg);
		float x = peg.pos.x;
		float y = peg.pos.y;

		// Terracotta / Amber instrument collar ring
		nvgBeginPath(vg);
		nvgCircle(vg, x, y, 4.9f);
		nvgStrokeColor(vg, nvgRGB(205, 72, 34));
		nvgStrokeWidth(vg, 1.1f);
		nvgStroke(vg);

		// Spun stainless steel top cap
		nvgBeginPath(vg);
		nvgCircle(vg, x, y, 4.1f);
		NVGpaint capGrad = nvgLinearGradient(
			vg, x - 4.f, y - 4.f, x + 4.f, y + 4.f,
			nvgRGB(238, 242, 245),
			nvgRGB(145, 152, 160)
		);
		nvgFillPaint(vg, capGrad);
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGB(48, 52, 58));
		nvgStrokeWidth(vg, 0.8f);
		nvgStroke(vg);

		// Center hex socket
		nvgBeginPath(vg);
		nvgCircle(vg, x, y, 1.5f);
		nvgFillColor(vg, nvgRGB(52, 56, 62));
		nvgFill(vg);

		nvgRestore(vg);
	}

	// -------------------------------------------------------------
	// 4. DRAW ZIP TIE BANDS
	// -------------------------------------------------------------
	if (module) {
		for (const ZipTie& z : module->zipTies) {
			int numWires = (int)z.cableIds.size();
			if (numWires == 0) continue;

			float bundleWidth = (numWires - 1) * z.wireSpacing;
			float bandLength = bundleWidth + 12.0f;
			float bandThickness = z.width;

			bool isHovered = (z.id == hoveredTieId);
			bool isDragged = (z.id == draggedTieId);

			nvgSave(vg);
			nvgAlpha(vg, std::pow(opacity, 1.5f));
			nvgTranslate(vg, z.pos.x, z.pos.y);
			float angle = std::atan2(z.normal.y, z.normal.x);
			nvgRotate(vg, angle);

			// Zip Tie Drop Shadow
			float shadowOffset = isDragged ? 4.0f : 2.0f;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, -bandLength * 0.5f - 1.f, -bandThickness * 0.5f + shadowOffset, bandLength + 2.f, bandThickness, 3.0f);
			nvgFillColor(vg, nvgRGBAf(0.f, 0.f, 0.f, isDragged ? 0.35f : 0.25f));
			nvgFill(vg);

			// Zip Tie Body
			nvgBeginPath(vg);
			nvgRoundedRect(vg, -bandLength * 0.5f, -bandThickness * 0.5f, bandLength, bandThickness, 3.0f);
			nvgFillColor(vg, z.color);
			nvgFill(vg);

			// Inner Bevel / Rim
			bool isBlackTie = (z.color.r < 0.5f);
			float rimAlpha = isBlackTie ? 0.22f : 0.55f;
			nvgStrokeColor(vg, isHovered ? nvgRGBAf(0.3f, 0.7f, 1.0f, 0.9f) : nvgRGBAf(1.f, 1.f, 1.f, rimAlpha));
			nvgStrokeWidth(vg, isHovered ? 1.5f : 1.0f);
			nvgStroke(vg);

			// Zip Tie Lock Clasp / Buckle Head
			float claspW = 5.0f;
			float claspH = bandThickness + 3.0f;
			float claspX = bandLength * 0.5f - claspW - 0.5f;
			float claspY = -claspH * 0.5f;

			nvgBeginPath(vg);
			nvgRoundedRect(vg, claspX, claspY, claspW, claspH, 1.5f);
			nvgFillColor(vg, color::mult(z.color, 0.85f));
			nvgFill(vg);
			nvgStrokeColor(vg, nvgRGBAf(0.f, 0.f, 0.f, 0.3f));
			nvgStrokeWidth(vg, 1.0f);
			nvgStroke(vg);

			// Little ratcheting slot detail inside clasp
			nvgBeginPath(vg);
			nvgRect(vg, claspX + 1.5f, -1.5f, 1.5f, 3.0f);
			nvgFillColor(vg, nvgRGBAf(0.1f, 0.1f, 0.15f, 0.45f));
			nvgFill(vg);

			nvgRestore(vg);
		}
	}

	// -------------------------------------------------------------
	// 5. DRAW SLICING / CUT LINE AND TOOLTIP
	// -------------------------------------------------------------
	if (isCutting) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, cutStartPos.x, cutStartPos.y);
		nvgLineTo(vg, cutCurrentPos.x, cutCurrentPos.y);
		nvgStrokeColor(vg, nvgRGBAf(0.35f, 0.65f, 1.0f, 0.35f));
		nvgStrokeWidth(vg, 6.0f);
		nvgStroke(vg);

		nvgBeginPath(vg);
		nvgMoveTo(vg, cutStartPos.x, cutStartPos.y);
		nvgLineTo(vg, cutCurrentPos.x, cutCurrentPos.y);
		nvgStrokeColor(vg, nvgRGBAf(0.70f, 0.85f, 1.0f, 0.95f));
		nvgStrokeWidth(vg, 2.0f);
		nvgStroke(vg);

		nvgBeginPath(vg);
		nvgCircle(vg, cutStartPos.x, cutStartPos.y, 4.0f);
		nvgFillColor(vg, nvgRGBAf(0.35f, 0.65f, 1.0f, 0.4f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, cutStartPos.x, cutStartPos.y, 2.0f);
		nvgFillColor(vg, nvgRGB(255, 255, 255));
		nvgFill(vg);

		nvgBeginPath(vg);
		nvgCircle(vg, cutCurrentPos.x, cutCurrentPos.y, 4.0f);
		nvgFillColor(vg, nvgRGBAf(0.35f, 0.65f, 1.0f, 0.4f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, cutCurrentPos.x, cutCurrentPos.y, 2.0f);
		nvgFillColor(vg, nvgRGB(255, 255, 255));
		nvgFill(vg);

		int count = (int)intersectedCables.size();
		std::string tipText;
		if (count == 0) {
			tipText = "Slice wires to tie";
		}
		else if (count == 1) {
			tipText = "Tie 1 wire";
		}
		else {
			tipText = string::f("Tie %d wires", count);
		}

		float tipX = cutCurrentPos.x + 16.0f;
		float tipY = cutCurrentPos.y + 12.0f;
		float tipPaddingX = 10.0f;
		float tipH = 24.0f;

		nvgFontSize(vg, 12.0f);
		nvgFontFaceId(vg, APP->window->uiFont->handle);
		float bounds[4];
		nvgTextBounds(vg, 0, 0, tipText.c_str(), NULL, bounds);
		float tipW = (bounds[2] - bounds[0]) + tipPaddingX * 2.0f;

		nvgBeginPath(vg);
		nvgRoundedRect(vg, tipX, tipY + 2.0f, tipW, tipH, 4.0f);
		nvgFillColor(vg, nvgRGBAf(0.f, 0.f, 0.f, 0.35f));
		nvgFill(vg);

		nvgBeginPath(vg);
		nvgRoundedRect(vg, tipX, tipY, tipW, tipH, 4.0f);
		nvgFillColor(vg, nvgRGBAf(0.08f, 0.10f, 0.14f, 0.94f));
		nvgFill(vg);
		nvgStrokeColor(vg, count > 0 ? nvgRGBAf(0.35f, 0.65f, 1.0f, 0.7f) : nvgRGBAf(0.3f, 0.35f, 0.45f, 0.5f));
		nvgStrokeWidth(vg, 1.2f);
		nvgStroke(vg);

		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, count > 0 ? nvgRGB(240, 248, 255) : nvgRGB(170, 180, 195));
		nvgText(vg, tipX + tipW * 0.5f, tipY + tipH * 0.5f, tipText.c_str(), NULL);
	}

	nvgRestore(vg);
	TransparentWidget::drawLayer(args, layer);
}

} // namespace ZipTies
