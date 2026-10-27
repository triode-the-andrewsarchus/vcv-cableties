#pragma once
#include <rack.hpp>
#include "ZipTieModel.hpp"
#include "ZipTieModule.hpp"
#include "PegModule.hpp"
#include <vector>
#include <map>
#include <set>
#include <unordered_set>

namespace ZipTies {

using namespace rack;

struct IntersectedCableInfo {
	int64_t cableId;
	app::CableWidget* cw = nullptr;
	math::Vec intersectionPoint;
	int segmentIndex = 0; // Which segment of the cable was intersected
};

struct KeepOutObstacle {
	bool isBox = false;
	float xMin = 0.f;
	float xMax = 0.f;
	float yTop = 0.f;
	float yBot = 0.f;

	// Visual peg center positions (before deflection clearance margin)
	float pegLeftX = 0.f;
	float pegRightX = 0.f;
	float pegTopY = 0.f;
	float pegBotY = 0.f;

	// Per-column peg Y positions for 2D keep-out quadrilaterals (when linked columns differ in height)
	float pegLeftTopY = 0.f;
	float pegLeftBotY = 0.f;
	float pegRightTopY = 0.f;
	float pegRightBotY = 0.f;

	float pegTopAtX(float x) const {
		if (!isBox || pegRightX <= pegLeftX + 1e-3f) return pegTopY;
		float t = math::clamp((x - pegLeftX) / (pegRightX - pegLeftX), 0.f, 1.f);
		return pegLeftTopY + t * (pegRightTopY - pegLeftTopY);
	}

	float pegBotAtX(float x) const {
		if (!isBox || pegRightX <= pegLeftX + 1e-3f) return pegBotY;
		float t = math::clamp((x - pegLeftX) / (pegRightX - pegLeftX), 0.f, 1.f);
		return pegLeftBotY + t * (pegRightBotY - pegLeftBotY);
	}
};

struct ActivePegCap {
	math::Vec pos;
};

struct HarnessOverlayWidget : widget::TransparentWidget {
	ZipTieModule* module = nullptr;
	app::ModuleWidget* ownerMw = nullptr;
	std::set<app::ModuleWidget*> registeredWidgets;

	// Dragging cut line state
	bool isCutting = false;
	math::Vec cutStartPos;
	math::Vec cutCurrentPos;
	std::vector<IntersectedCableInfo> intersectedCables;

	// Dragging existing zip tie state
	int draggedTieId = -1;
	math::Vec dragOffset;
	int hoveredTieId = -1;

	// Cached cable paths & keep-out obstacles for efficient drawing and intersection
	std::vector<CablePath> cachedTiedPaths;
	std::vector<CablePath> cachedUntiedPaths;
	std::unordered_set<int64_t> deflectedUntiedCableIds;
	std::vector<KeepOutObstacle> cachedObstacles;
	std::vector<ActivePegCap> cachedActivePegs;

	static HarnessOverlayWidget* acquire(app::ModuleWidget* mw);
	static void release(app::ModuleWidget* mw);

	HarnessOverlayWidget();
	~HarnessOverlayWidget() override;

	void step() override;
	void drawLayer(const DrawArgs& args, int layer) override;

	void onButton(const ButtonEvent& e) override;
	void onDragStart(const DragStartEvent& e) override;
	void onDragMove(const DragMoveEvent& e) override;
	void onDragEnd(const DragEndEvent& e) override;
	void onHover(const HoverEvent& e) override;
	void onHoverKey(const HoverKeyEvent& e) override;
	void onSelectKey(const SelectKeyEvent& e) override;

	bool handleKeyLearn(int key, int action, int mods);
	bool isShortcutActive(int mods = 0) const;
	void buildKeepOutObstacles();
	void updateCablePaths(const std::unordered_map<int64_t, app::CableWidget*>& activeCables);
	void updateCutIntersections();
};

} // namespace ZipTies
