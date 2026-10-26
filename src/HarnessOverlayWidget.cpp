#include "HarnessOverlayWidget.hpp"
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <settings.hpp>
#include <GLFW/glfw3.h>

namespace ZipTies {

struct Waypoint {
	rack::math::Vec pos;
	rack::math::Vec tangent;
	bool isPort = false;
	Waypoint(rack::math::Vec p, rack::math::Vec t, bool port = false) : pos(p), tangent(t), isPort(port) {}
};

HarnessOverlayWidget::HarnessOverlayWidget() {
	box.pos = math::Vec(0, 0);
	if (APP && APP->scene && APP->scene->rack) {
		box.size = APP->scene->rack->box.size;
	}
}

HarnessOverlayWidget::~HarnessOverlayWidget() {
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

	// Check required modifier flags
	if ((currentMods & reqMods) != reqMods)
		return false;

	// Check primary key if one is specified
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
			// Accumulate modifier in case user is pressing a combo or modifier-only shortcut
			module->pendingMods |= modifierBitForKey(key) | (mods & RACK_MOD_MASK);
			return true;
		}
		else if (key > 0) {
			// Non-modifier key pressed: bind (key + held mods) immediately
			int finalMods = (mods & RACK_MOD_MASK) | module->pendingMods;
			module->setShortcut(key, finalMods);
			return true;
		}
	}
	else if (action == GLFW_RELEASE) {
		if (isModifierKey(key) && module->pendingMods != 0) {
			// Released modifier without pressing another key -> bind modifier-only combo
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

void HarnessOverlayWidget::step() {
	if (!module) return;

	// Keep overlay matched to rack dimensions
	if (APP && APP->scene && APP->scene->rack) {
		box.pos = math::Vec(0, 0);
		box.size = APP->scene->rack->box.size;
	}

	// Clean any unplugged/deleted cables
	module->cleanDeadCables();

	// Ensure tied cables are hidden from Rack's default renderer
	// and untied cables are visible
	if (APP && APP->scene && APP->scene->rack) {
		for (app::CableWidget* cw : APP->scene->rack->getCompleteCables()) {
			if (!cw->cable) continue;
			bool tied = module->isCableTied(cw->cable->id);
			if (tied) {
				if (cw->visible) cw->visible = false;
			}
			else {
				if (!cw->visible) cw->visible = true;
			}
		}
	}

	// Update cached geometry for all paths
	updateCablePaths();

	// If cutting, update current mouse position and intersections
	if (isCutting && APP && APP->scene && APP->scene->rack) {
		cutCurrentPos = APP->scene->rack->getMousePos();
		updateCutIntersections();
	}

	// Hit test zip ties for hover state
	if (APP && APP->scene && APP->scene->rack) {
		math::Vec mouse = APP->scene->rack->getMousePos();
		hoveredTieId = -1;
		for (const ZipTie& z : module->zipTies) {
			if (mouse.minus(z.pos).norm() <= 16.0f) {
				hoveredTieId = z.id;
				break;
			}
		}
	}

	TransparentWidget::step();
}

void HarnessOverlayWidget::updateCablePaths() {
	cachedTiedPaths.clear();
	cachedUntiedPaths.clear();
	if (!module || !APP || !APP->scene || !APP->scene->rack) return;

	std::lock_guard<std::recursive_mutex> lock(module->stateMutex);

	// STEP 1: Compute Tangent, Normal, and Ordered Wires for each Zip Tie
	for (ZipTie& z : module->zipTies) {
		math::Vec inVec(0.f, 0.f);
		math::Vec outVec(0.f, 0.f);
		int inCount = 0;
		int outCount = 0;

		for (int64_t cid : z.cableIds) {
			app::CableWidget* cw = APP->scene->rack->getCable(cid);
			if (!cw || !cw->isComplete()) continue;

			auto it = module->cableTieSequences.find(cid);
			if (it == module->cableTieSequences.end()) continue;
			const auto& seq = it->second;

			// Find index of this tie in the cable's sequence
			auto tieIt = std::find(seq.begin(), seq.end(), z.id);
			if (tieIt == seq.end()) continue;
			int idx = (int)(tieIt - seq.begin());

			// Previous waypoint
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

			// Next waypoint
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
			app::CableWidget* cwa = APP->scene->rack->getCable(a);
			app::CableWidget* cwb = APP->scene->rack->getCable(b);
			if (!cwa || !cwb) return a < b;
			math::Vec pa = cwa->getOutputPos();
			math::Vec pb = cwb->getOutputPos();
			float proja = pa.minus(z.pos).dot(z.normal);
			float projb = pb.minus(z.pos).dot(z.normal);
			return proja < projb;
		});
	}

	// STEP 2: Compute Full Paths for Tied Cables
	for (const auto& pair : module->cableTieSequences) {
		int64_t cid = pair.first;
		const auto& seq = pair.second;
		app::CableWidget* cw = APP->scene->rack->getCable(cid);
		if (!cw || !cw->isComplete() || seq.empty()) continue;

		CablePath cp;
		cp.cableId = cid;
		cp.cw = cw;

		math::Vec outPos = cw->getOutputPos();
		math::Vec inPos = cw->getInputPos();

		// Collect all waypoints and tangents for this cable
		std::vector<Waypoint> waypoints;

		// Start at Output Port
		waypoints.push_back(Waypoint(outPos, math::Vec(0, 1.f), true));

		// Zip Tie Waypoints
		for (int tid : seq) {
			const ZipTie* z = module->getZipTie(tid);
			if (!z) continue;

			// Find wire offset in this tie's bundle
			int wireIdx = 0;
			int totalWires = (int)z->orderedCableIds.size();
			auto it = std::find(z->orderedCableIds.begin(), z->orderedCableIds.end(), cid);
			if (it != z->orderedCableIds.end()) {
				wireIdx = (int)(it - z->orderedCableIds.begin());
			}

			float offset = (wireIdx - (totalWires - 1) * 0.5f) * z->wireSpacing;
			math::Vec wirePos = z->pos.plus(z->normal.mult(offset));

			waypoints.push_back(Waypoint(wirePos, z->tangent, false));
		}

		// End at Input Port
		waypoints.push_back(Waypoint(inPos, math::Vec(0, -1.f), true));

		// Build cubic Bezier segments between adjacent waypoints
		for (size_t s = 0; s < waypoints.size() - 1; s++) {
			Waypoint& w0 = waypoints[s];
			Waypoint& w1 = waypoints[s + 1];

			math::Vec p0 = w0.pos;
			math::Vec p3 = w1.pos;

			// Endpoints are offset from port center by 14px (matching VCV Rack collar)
			if (w0.isPort) {
				math::Vec dir = p3.minus(p0).normalize();
				p0 = p0.plus(dir.mult(14.f));
			}
			if (w1.isPort) {
				math::Vec dir = p0.minus(p3).normalize();
				p3 = p3.plus(dir.mult(14.f));
			}

			float dist = p3.minus(p0).norm();
			math::Vec t0 = w0.tangent;
			math::Vec t1 = w1.tangent;

			if (w0.isPort) {
				t0 = p3.minus(p0).normalize().plus(math::Vec(0.f, 0.7f)).normalize();
			}
			else {
				// Make sure tangent points towards next waypoint
				if (t0.dot(p3.minus(p0)) < 0.f) t0 = t0.neg();
			}

			if (w1.isPort) {
				t1 = p3.minus(p0).normalize().plus(math::Vec(0.f, -0.7f)).normalize();
			}
			else {
				if (t1.dot(p3.minus(p0)) < 0.f) t1 = t1.neg();
			}

			math::Vec p1 = p0.plus(t0.mult(dist * 0.38f));
			math::Vec p2 = p3.minus(t1.mult(dist * 0.38f));

			// Tension / sag droop
			float sag = (1.0f - settings::cableTension) * (18.0f + 0.12f * dist);
			p1.y += sag;
			p2.y += sag;

			CableSegmentCurve curve;
			curve.p0 = p0;
			curve.p1 = p1;
			curve.p2 = p2;
			curve.p3 = p3;
			curve.isQuad = false;
			cp.segments.push_back(curve);

			// Sample segment for intersection hit-testing
			const int NUM_SAMPLES = 25;
			for (int k = 0; k <= NUM_SAMPLES; k++) {
				float t = (float)k / (float)NUM_SAMPLES;
				math::Vec pt = evalCubicBezier(p0, p1, p2, p3, t);
				cp.sampledPoints.push_back(pt);
				cp.sampleSegmentIndices.push_back((int)s);
			}
		}

		cachedTiedPaths.push_back(cp);
	}

	// STEP 3: Compute Paths for Untied Cables (for cutting hit-testing)
	for (app::CableWidget* cw : APP->scene->rack->getCompleteCables()) {
		if (!cw->cable) continue;
		if (module->isCableTied(cw->cable->id)) continue;

		CablePath cp;
		cp.cableId = cw->cable->id;
		cp.cw = cw;

		math::Vec outPos = cw->getOutputPos();
		math::Vec inPos = cw->getInputPos();
		float dist = outPos.minus(inPos).norm();
		math::Vec slump = outPos.plus(inPos).div(2.f);
		slump.y += (1.0f - settings::cableTension) * (150.0f + 1.0f * dist);

		outPos = outPos.plus(slump.minus(outPos).normalize().mult(14.f));
		inPos = inPos.plus(slump.minus(inPos).normalize().mult(14.f));

		CableSegmentCurve curve;
		curve.p0 = outPos;
		curve.qControl = slump;
		curve.p3 = inPos;
		curve.isQuad = true;
		cp.segments.push_back(curve);

		const int NUM_SAMPLES = 30;
		for (int k = 0; k <= NUM_SAMPLES; k++) {
			float t = (float)k / (float)NUM_SAMPLES;
			math::Vec pt = evalQuadBezier(outPos, slump, inPos, t);
			cp.sampledPoints.push_back(pt);
			cp.sampleSegmentIndices.push_back(0);
		}

		cachedUntiedPaths.push_back(cp);
	}
}

void HarnessOverlayWidget::updateCutIntersections() {
	intersectedCables.clear();
	if (!isCutting) return;

	std::set<int64_t> seenCableIds;

	// Check tied paths
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
				info.segmentIndex = cp.sampleSegmentIndices[i];
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
	if (!module) {
		TransparentWidget::onButton(e);
		return;
	}

	// Always allow clicks on our own ZipTie module panel to pass through
	// so the user can toggle ARM TOOL, SNIP ALL, or adjust knobs at any time
	if (ownerMw && ownerMw->box.contains(e.pos)) {
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
				// Create new Zip Tie at centroid of cut intersections
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
	if (!module) return;

	// Layer 3 is where RackWidget draws cables and plugs
	if (layer != 3) {
		TransparentWidget::drawLayer(args, layer);
		return;
	}

	NVGcontext* vg = args.vg;
	float opacity = settings::cableOpacity;
	if (opacity <= 0.0f) return;

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
		for (const CableSegmentCurve& sc : cp.segments) {
			math::Vec shP0 = sc.p0.plus(math::Vec(0.f, 30.f));
			math::Vec shP1 = sc.p1.plus(math::Vec(0.f, 30.f));
			math::Vec shP2 = sc.p2.plus(math::Vec(0.f, 30.f));
			math::Vec shP3 = sc.p3.plus(math::Vec(0.f, 30.f));

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
	// 3. DRAW ZIP TIE BANDS
	// -------------------------------------------------------------
	for (const ZipTie& z : module->zipTies) {
		int numWires = (int)z.cableIds.size();
		if (numWires == 0) continue;

		float bundleWidth = (numWires - 1) * z.wireSpacing;
		float bandLength = bundleWidth + 12.0f; // Band spans across bundle
		float bandThickness = z.width;          // Band width along cable

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
		nvgStrokeColor(vg, isHovered ? nvgRGBAf(0.3f, 0.7f, 1.0f, 0.9f) : nvgRGBAf(1.f, 1.f, 1.f, 0.55f));
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

	// -------------------------------------------------------------
	// 4. DRAW SLICING / CUT LINE AND TOOLTIP (Ala Screenshots)
	// -------------------------------------------------------------
	if (isCutting) {
		// Glow behind cut line
		nvgBeginPath(vg);
		nvgMoveTo(vg, cutStartPos.x, cutStartPos.y);
		nvgLineTo(vg, cutCurrentPos.x, cutCurrentPos.y);
		nvgStrokeColor(vg, nvgRGBAf(0.35f, 0.65f, 1.0f, 0.35f));
		nvgStrokeWidth(vg, 6.0f);
		nvgStroke(vg);

		// Crisp electric-blue cut line
		nvgBeginPath(vg);
		nvgMoveTo(vg, cutStartPos.x, cutStartPos.y);
		nvgLineTo(vg, cutCurrentPos.x, cutCurrentPos.y);
		nvgStrokeColor(vg, nvgRGBAf(0.70f, 0.85f, 1.0f, 0.95f));
		nvgStrokeWidth(vg, 2.0f);
		nvgStroke(vg);

		// Start Pin Dot
		nvgBeginPath(vg);
		nvgCircle(vg, cutStartPos.x, cutStartPos.y, 4.0f);
		nvgFillColor(vg, nvgRGBAf(0.35f, 0.65f, 1.0f, 0.4f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, cutStartPos.x, cutStartPos.y, 2.0f);
		nvgFillColor(vg, nvgRGB(255, 255, 255));
		nvgFill(vg);

		// Current Cursor Pin Dot
		nvgBeginPath(vg);
		nvgCircle(vg, cutCurrentPos.x, cutCurrentPos.y, 4.0f);
		nvgFillColor(vg, nvgRGBAf(0.35f, 0.65f, 1.0f, 0.4f));
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, cutCurrentPos.x, cutCurrentPos.y, 2.0f);
		nvgFillColor(vg, nvgRGB(255, 255, 255));
		nvgFill(vg);

		// Tooltip Box (Matching Screenshot 1: "Tie 3 wires")
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

		// Calculate text width
		nvgFontSize(vg, 12.0f);
		nvgFontFaceId(vg, APP->window->uiFont->handle);
		float bounds[4];
		nvgTextBounds(vg, 0, 0, tipText.c_str(), NULL, bounds);
		float tipW = (bounds[2] - bounds[0]) + tipPaddingX * 2.0f;

		// Tooltip drop shadow
		nvgBeginPath(vg);
		nvgRoundedRect(vg, tipX, tipY + 2.0f, tipW, tipH, 4.0f);
		nvgFillColor(vg, nvgRGBAf(0.f, 0.f, 0.f, 0.35f));
		nvgFill(vg);

		// Tooltip container
		nvgBeginPath(vg);
		nvgRoundedRect(vg, tipX, tipY, tipW, tipH, 4.0f);
		nvgFillColor(vg, nvgRGBAf(0.08f, 0.10f, 0.14f, 0.94f));
		nvgFill(vg);
		nvgStrokeColor(vg, count > 0 ? nvgRGBAf(0.35f, 0.65f, 1.0f, 0.7f) : nvgRGBAf(0.3f, 0.35f, 0.45f, 0.5f));
		nvgStrokeWidth(vg, 1.2f);
		nvgStroke(vg);

		// Tooltip Text
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(vg, count > 0 ? nvgRGB(240, 248, 255) : nvgRGB(170, 180, 195));
		nvgText(vg, tipX + tipW * 0.5f, tipY + tipH * 0.5f, tipText.c_str(), NULL);
	}

	nvgRestore(vg);
	TransparentWidget::drawLayer(args, layer);
}

} // namespace ZipTies
