#pragma once
#include <rack.hpp>
#include "ZipTieModel.hpp"
#include "ZipTieModule.hpp"
#include <vector>
#include <map>

namespace ZipTies {

using namespace rack;

struct IntersectedCableInfo {
	int64_t cableId;
	app::CableWidget* cw = nullptr;
	math::Vec intersectionPoint;
	int segmentIndex = 0; // Which segment of the cable was intersected
};

struct HarnessOverlayWidget : widget::TransparentWidget {
	ZipTieModule* module = nullptr;
	app::ModuleWidget* ownerMw = nullptr;

	// Dragging cut line state
	bool isCutting = false;
	math::Vec cutStartPos;
	math::Vec cutCurrentPos;
	std::vector<IntersectedCableInfo> intersectedCables;

	// Dragging existing zip tie state
	int draggedTieId = -1;
	math::Vec dragOffset;
	int hoveredTieId = -1;

	// Cached cable paths for efficient drawing and intersection
	std::vector<CablePath> cachedTiedPaths;
	std::vector<CablePath> cachedUntiedPaths;

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
	void updateCablePaths(const std::unordered_map<int64_t, app::CableWidget*>& activeCables);
	void updateCutIntersections();
};

} // namespace ZipTies
