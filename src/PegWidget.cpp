#include "plugin.hpp"
#include "PegModule.hpp"
#include "HarnessOverlayWidget.hpp"
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <settings.hpp>
#include <cmath>

namespace ZipTies {

// Helper to find all PegWidgets currently in the rack
static std::vector<PegWidget*> getAllPegWidgets() {
	std::vector<PegWidget*> result;
	if (!APP || !APP->scene || !APP->scene->rack) return result;
	widget::Widget* mc = APP->scene->rack->getModuleContainer();
	if (!mc) return result;
	for (widget::Widget* w : mc->children) {
		PegWidget* pw = dynamic_cast<PegWidget*>(w);
		if (pw && pw->module) {
			result.push_back(pw);
		}
	}
	return result;
}

static PegWidget* findPegWidgetById(int64_t moduleId) {
	if (moduleId < 0 || !APP || !APP->scene || !APP->scene->rack) return nullptr;
	app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);
	return dynamic_cast<PegWidget*>(mw);
}

static PegWidget* findVerticallyAlignedNeighbor(PegWidget* self, bool isTopEdge) {
	if (!self || !self->module) return nullptr;
	float selfX = self->box.pos.x;
	float targetY = isTopEdge ? (self->box.pos.y - RACK_GRID_HEIGHT) : (self->box.pos.y + RACK_GRID_HEIGHT);
	for (PegWidget* pw : getAllPegWidgets()) {
		if (pw == self) continue;
		if (std::fabs(pw->box.pos.x - selfX) < 1.5f && std::fabs(pw->box.pos.y - targetY) < 1.5f) {
			return pw;
		}
	}
	return nullptr;
}

static PegWidget* findNearestHorizontalNeighbor(PegWidget* self, bool toLeft) {
	if (!self || !self->module) return nullptr;
	float selfX = self->box.pos.x;
	float selfY = self->box.pos.y;
	PegWidget* best = nullptr;
	float bestDist = 1e9f;

	for (PegWidget* pw : getAllPegWidgets()) {
		if (pw == self) continue;
		if (std::fabs(pw->box.pos.y - selfY) > 1.5f) continue;
		float dx = pw->box.pos.x - selfX;
		if (toLeft && dx < -1.5f) {
			float dist = -dx;
			if (dist < bestDist) {
				bestDist = dist;
				best = pw;
			}
		}
		else if (!toLeft && dx > 1.5f) {
			float dist = dx;
			if (dist < bestDist) {
				bestDist = dist;
				best = pw;
			}
		}
	}
	return best;
}

// ============================================================================
// 1. VERTICAL ALIGNMENT LINK BUTTON (Appears only when vertically aligned)
// ============================================================================
struct VerticalLinkButton : widget::OpaqueWidget {
	PegWidget* owner = nullptr;
	bool isTopEdge = false;

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (owner && owner->module) {
				PegModule* m = dynamic_cast<PegModule*>(owner->module);
				PegWidget* neighborW = findVerticallyAlignedNeighbor(owner, isTopEdge);
				PegModule* neighborM = neighborW ? dynamic_cast<PegModule*>(neighborW->module) : nullptr;
				if (m && neighborM) {
					if (isTopEdge) {
						if (m->linkedAboveId == neighborM->id) {
							// Manually unlink
							m->linkedAboveId = -1;
							m->manualUnlinkAboveId = neighborM->id;
							if (neighborM->linkedBelowId == m->id) neighborM->linkedBelowId = -1;
							neighborM->manualUnlinkBelowId = m->id;
						}
						else {
							// Re-link
							m->linkedAboveId = neighborM->id;
							m->manualUnlinkAboveId = -1;
							neighborM->linkedBelowId = m->id;
							neighborM->manualUnlinkBelowId = -1;
						}
					}
					else {
						if (m->linkedBelowId == neighborM->id) {
							// Manually unlink
							m->linkedBelowId = -1;
							m->manualUnlinkBelowId = neighborM->id;
							if (neighborM->linkedAboveId == m->id) neighborM->linkedAboveId = -1;
							neighborM->manualUnlinkAboveId = m->id;
						}
						else {
							// Re-link
							m->linkedBelowId = neighborM->id;
							m->manualUnlinkBelowId = -1;
							neighborM->linkedAboveId = m->id;
							neighborM->manualUnlinkAboveId = -1;
						}
					}
				}
			}
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void draw(const DrawArgs& args) override {
		if (!owner || !owner->module) return;
		PegModule* m = dynamic_cast<PegModule*>(owner->module);
		if (!m) return;

		bool linked = isTopEdge ? (m->linkedAboveId != -1) : (m->linkedBelowId != -1);

		NVGcontext* vg = args.vg;
		nvgSave(vg);

		float w = box.size.x;
		float h = box.size.y;

		// Outer bezel
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.f, 0.f, w, h, 2.0f);
		nvgFillColor(vg, nvgRGB(20, 23, 26));
		nvgFill(vg);

		// Inner button cap
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 1.0f, 1.0f, w - 2.0f, h - 2.0f, 1.5f);
		if (linked) {
			NVGpaint grad = nvgLinearGradient(
				vg, 0.f, 1.f, 0.f, h - 1.f,
				nvgRGB(224, 96, 38),
				nvgRGB(178, 56, 20)
			);
			nvgFillPaint(vg, grad);
		}
		else {
			NVGpaint grad = nvgLinearGradient(
				vg, 0.f, 1.f, 0.f, h - 1.f,
				nvgRGB(68, 74, 82),
				nvgRGB(42, 46, 52)
			);
			nvgFillPaint(vg, grad);
		}
		nvgFill(vg);

		nvgStrokeColor(vg, linked ? nvgRGBA(255, 215, 160, 190) : nvgRGBA(255, 255, 255, 70));
		nvgStrokeWidth(vg, 0.7f);
		nvgStroke(vg);

		if (APP && APP->window && APP->window->uiFont) {
			nvgFontFaceId(vg, APP->window->uiFont->handle);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFontSize(vg, 5.5f);
			nvgTextLetterSpacing(vg, 0.3f);
			nvgFillColor(vg, linked ? nvgRGB(255, 248, 235) : nvgRGB(215, 222, 230));
			const char* label = linked
				? (isTopEdge ? "▲ LNK" : "▼ LNK")
				: (isTopEdge ? "▲ LINK" : "▼ LINK");
			nvgText(vg, w * 0.5f + 0.2f, h * 0.5f + 0.2f, label, NULL);
		}

		nvgRestore(vg);
	}
};

// ============================================================================
// 2. 2HP PEG PANEL TYPOGRAPHY, GUIDE CHANNEL & PEG GRAPHICS
// ============================================================================
struct PegPanelGraphicsWidget : widget::TransparentWidget {
	PegWidget* owner = nullptr;

	static void drawTurretPeg(NVGcontext* vg, float x, float y, bool dark) {
		nvgSave(vg);

		// Cast shadow onto panel
		nvgBeginPath(vg);
		nvgCircle(vg, x + 0.8f, y + 1.8f, 8.2f);
		nvgFillColor(vg, nvgRGBA(0, 0, 0, dark ? 150 : 95));
		nvgFill(vg);

		// Outer knurled brass/steel base flange (r = 7.8)
		nvgBeginPath(vg);
		nvgCircle(vg, x, y, 7.8f);
		NVGpaint baseGrad = nvgLinearGradient(
			vg, x - 7.8f, y - 7.8f, x + 7.8f, y + 7.8f,
			nvgRGB(95, 102, 110),
			nvgRGB(42, 46, 52)
		);
		nvgFillPaint(vg, baseGrad);
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGB(20, 23, 26));
		nvgStrokeWidth(vg, 1.0f);
		nvgStroke(vg);

		// Recessed rubber/Delrin cable-guide collar groove (r = 6.0)
		nvgBeginPath(vg);
		nvgCircle(vg, x, y, 6.0f);
		nvgFillColor(vg, nvgRGB(26, 29, 33));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 45));
		nvgStrokeWidth(vg, 0.6f);
		nvgStroke(vg);

		// Terracotta / Amber instrument collar ring (r = 4.9)
		nvgBeginPath(vg);
		nvgCircle(vg, x, y, 4.9f);
		nvgStrokeColor(vg, nvgRGB(205, 72, 34));
		nvgStrokeWidth(vg, 1.1f);
		nvgStroke(vg);

		// Spun stainless steel top cap (r = 4.1)
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

		// Center hex/Allen socket detail
		nvgBeginPath(vg);
		nvgCircle(vg, x, y, 1.5f);
		nvgFillColor(vg, nvgRGB(52, 56, 62));
		nvgFill(vg);

		nvgRestore(vg);
	}

	static void drawFlushSocket(NVGcontext* vg, float x, float y, bool dark) {
		// Visually removed peg (flush countersunk blanking plug when vertically linked)
		nvgSave(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, x, y, 3.8f);
		nvgFillColor(vg, dark ? nvgRGB(32, 36, 40) : nvgRGB(184, 188, 182));
		nvgFill(vg);
		nvgStrokeColor(vg, dark ? nvgRGB(64, 70, 78) : nvgRGB(128, 132, 126));
		nvgStrokeWidth(vg, 0.8f);
		nvgStroke(vg);

		nvgBeginPath(vg);
		nvgCircle(vg, x, y, 1.2f);
		nvgFillColor(vg, dark ? nvgRGB(55, 60, 68) : nvgRGB(142, 146, 140));
		nvgFill(vg);
		nvgRestore(vg);
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		if (!APP || !APP->window || !APP->window->uiFont) return;

		bool dark = settings::preferDarkPanels;
		PegModule* m = (owner && owner->module) ? dynamic_cast<PegModule*>(owner->module) : nullptr;

		bool linkedAbove = m && (m->linkedAboveId != -1);
		bool linkedBelow = m && (m->linkedBelowId != -1);
		bool linkedLeft  = m && (m->linkedLeftId != -1);
		bool linkedRight = m && (m->linkedRightId != -1);

		nvgSave(vg);
		nvgFontFaceId(vg, APP->window->uiFont->handle);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

		float cx = box.size.x * 0.5f;

		// 0. Dynamic NIM-BIN Outer Technical Frame
		// When linked above or below, open the frame across the shared seam so the two modules visually merge!
		float frameTop = linkedAbove ? 0.0f : 16.5f;
		float frameBot = linkedBelow ? box.size.y : 363.5f;
		NVGcolor frameCol = dark ? nvgRGB(80, 86, 94) : nvgRGB(43, 48, 53);

		nvgBeginPath(vg);
		if (!linkedAbove && !linkedBelow) {
			nvgRoundedRect(vg, 2.5f, frameTop, 25.0f, frameBot - frameTop, 1.2f);
		}
		else {
			// Side rails of the technical frame
			nvgMoveTo(vg, 2.5f, frameTop);
			nvgLineTo(vg, 2.5f, frameBot);
			nvgMoveTo(vg, 27.5f, frameTop);
			nvgLineTo(vg, 27.5f, frameBot);
			if (!linkedAbove) {
				nvgMoveTo(vg, 2.5f, frameTop);
				nvgLineTo(vg, 27.5f, frameTop);
			}
			if (!linkedBelow) {
				nvgMoveTo(vg, 2.5f, frameBot);
				nvgLineTo(vg, 27.5f, frameBot);
			}
		}
		nvgStrokeColor(vg, frameCol);
		nvgStrokeWidth(vg, 1.0f);
		nvgStroke(vg);

		// 1. Top NIM-BIN Header ("PEGS" + "GUARD") — ONLY drawn when NOT linked above!
		if (!linkedAbove) {
			// Top accent rule under header
			nvgBeginPath(vg);
			nvgMoveTo(vg, 2.5f, 42.0f);
			nvgLineTo(vg, 27.5f, 42.0f);
			nvgStrokeColor(vg, dark ? nvgRGB(224, 83, 46) : nvgRGB(184, 58, 27));
			nvgStrokeWidth(vg, 1.5f);
			nvgStroke(vg);

			nvgFontSize(vg, 8.2f);
			nvgTextLetterSpacing(vg, 0.8f);
			nvgFillColor(vg, dark ? nvgRGB(236, 240, 246) : nvgRGB(26, 30, 34));
			nvgText(vg, cx + 0.3f, 24.0f, "PEGS", NULL);

			// Terracotta Badge ("GUARD")
			nvgBeginPath(vg);
			nvgRoundedRect(vg, 4.0f, 30.8f, 22.0f, 8.6f, 1.4f);
			nvgFillColor(vg, dark ? nvgRGB(200, 68, 36) : nvgRGB(184, 58, 27));
			nvgFill(vg);

			nvgFontSize(vg, 5.5f);
			nvgTextLetterSpacing(vg, 0.4f);
			nvgFillColor(vg, nvgRGB(246, 245, 240));
			nvgText(vg, cx + 0.2f, 35.2f, "GUARD", NULL);
		}

		// 2. Vertical Keep-Out Channel Strip between active pegs
		// Stops cleanly at the mounting screw collar (y=16 / y=364) so it never runs through the screws
		float trackTop = linkedAbove ? 16.5f : PegModule::TOP_PEG_Y;
		float trackBot = linkedBelow ? (box.size.y - 16.5f) : PegModule::BOT_PEG_Y;

		// Recessed machined guide slot
		nvgBeginPath(vg);
		nvgRoundedRect(vg, cx - 2.5f, trackTop, 5.0f, trackBot - trackTop, 2.0f);
		nvgFillColor(vg, dark ? nvgRGB(22, 25, 28) : nvgRGB(172, 176, 170));
		nvgFill(vg);
		nvgStrokeColor(vg, dark ? nvgRGB(55, 60, 68) : nvgRGB(130, 134, 128));
		nvgStrokeWidth(vg, 0.8f);
		nvgStroke(vg);

		// Terracotta keep-out center tension line
		nvgBeginPath(vg);
		nvgMoveTo(vg, cx, trackTop + 1.0f);
		nvgLineTo(vg, cx, trackBot - 1.0f);
		nvgStrokeColor(vg, dark ? nvgRGBA(224, 86, 52, 210) : nvgRGBA(184, 58, 27, 200));
		nvgStrokeWidth(vg, 1.6f);
		nvgStroke(vg);

		// Graduated tick marks along vertical keep-out channel
		float tickStart = linkedAbove ? 46.0f : 78.0f;
		float tickEnd   = linkedBelow ? 334.0f : 302.0f;
		nvgBeginPath(vg);
		for (float y = tickStart; y <= tickEnd; y += 16.0f) {
			nvgMoveTo(vg, cx - 5.5f, y);
			nvgLineTo(vg, cx - 3.2f, y);
			nvgMoveTo(vg, cx + 3.2f, y);
			nvgLineTo(vg, cx + 5.5f, y);
		}
		nvgStrokeColor(vg, dark ? nvgRGBA(160, 168, 178, 120) : nvgRGBA(48, 52, 56, 120));
		nvgStrokeWidth(vg, 0.8f);
		nvgStroke(vg);

		// 3. Horizontal Link Status Badges (if linked to Left or Right peg module)
		if (linkedLeft || linkedRight) {
			float midY = 190.0f;
			nvgBeginPath(vg);
			nvgRoundedRect(vg, 3.5f, midY - 12.0f, 23.0f, 24.0f, 2.0f);
			nvgFillColor(vg, dark ? nvgRGBA(24, 28, 32, 230) : nvgRGBA(36, 40, 44, 225));
			nvgFill(vg);
			nvgStrokeColor(vg, nvgRGB(215, 78, 38));
			nvgStrokeWidth(vg, 0.9f);
			nvgStroke(vg);

			nvgFontSize(vg, 6.2f);
			nvgTextLetterSpacing(vg, 0.2f);
			nvgFillColor(vg, nvgRGB(250, 210, 175));
			std::string hLabel;
			if (linkedLeft && linkedRight) hLabel = "◄►";
			else if (linkedLeft) hLabel = "◄ L";
			else hLabel = "R ►";
			nvgText(vg, cx, midY - 4.0f, "SPAN", NULL);
			nvgFillColor(vg, nvgRGB(235, 100, 48));
			nvgText(vg, cx, midY + 5.0f, hLabel.c_str(), NULL);
		}

		// 4. Draw Top & Bottom Turret Pegs ONLY when active (removed cleanly when vertically linked!)
		if (!linkedAbove) {
			drawTurretPeg(vg, PegModule::PEG_X, PegModule::TOP_PEG_Y, dark);
		}

		if (!linkedBelow) {
			drawTurretPeg(vg, PegModule::PEG_X, PegModule::BOT_PEG_Y, dark);
		}

		// 5. Bottom NIM-BIN Footer ("NIM · 2HP") — ONLY drawn when NOT linked below!
		if (!linkedBelow) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, 2.5f, 352.0f);
			nvgLineTo(vg, 27.5f, 352.0f);
			nvgStrokeColor(vg, dark ? nvgRGB(61, 67, 74) : nvgRGB(43, 48, 53));
			nvgStrokeWidth(vg, 0.8f);
			nvgStroke(vg);

			nvgFontSize(vg, 5.8f);
			nvgTextLetterSpacing(vg, 0.8f);
			nvgFillColor(vg, dark ? nvgRGB(145, 153, 164) : nvgRGB(42, 46, 50));
			nvgText(vg, cx + 0.3f, 358.0f, "NIM · 2HP", NULL);
		}

		nvgRestore(vg);
	}
};

// ============================================================================
// 3. PEGWIDGET IMPLEMENTATION
// ============================================================================
PegWidget::PegWidget(PegModule* module) {
	setModule(module);
	setPanel(createPanel(
		asset::plugin(pluginInstance, "res/CablePegs.svg"),
		asset::plugin(pluginInstance, "res/CablePegs_dark.svg")
	));

	// Panel Graphics & Turret Pegs
	PegPanelGraphicsWidget* panelGraphics = new PegPanelGraphicsWidget;
	panelGraphics->owner = this;
	panelGraphics->box.pos = math::Vec(0, 0);
	panelGraphics->box.size = box.size;
	addChild(panelGraphics);

	// Centered 2HP Screws
	addChild(createWidget<ThemedScrew>(math::Vec(box.size.x * 0.5f - RACK_GRID_WIDTH * 0.5f, 0)));
	addChild(createWidget<ThemedScrew>(math::Vec(box.size.x * 0.5f - RACK_GRID_WIDTH * 0.5f, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

	// Top Vertical Alignment Link Button (shown when another PegWidget is directly above)
	VerticalLinkButton* topBtn = new VerticalLinkButton;
	topBtn->owner = this;
	topBtn->isTopEdge = true;
	topBtn->box.pos = math::Vec(3.5f, 22.0f);
	topBtn->box.size = math::Vec(23.0f, 11.5f);
	topBtn->visible = false;
	addChild(topBtn);
	topLinkBtn = topBtn;

	// Bottom Vertical Alignment Link Button (shown when another PegWidget is directly below)
	VerticalLinkButton* botBtn = new VerticalLinkButton;
	botBtn->owner = this;
	botBtn->isTopEdge = false;
	botBtn->box.pos = math::Vec(3.5f, 346.5f);
	botBtn->box.size = math::Vec(23.0f, 11.5f);
	botBtn->visible = false;
	addChild(botBtn);
	botLinkBtn = botBtn;

	if (module) {
		HarnessOverlayWidget::acquire(this);
	}
}

PegWidget::~PegWidget() {
	// Clean up mutual links if this widget is removed
	if (module) {
		PegModule* m = dynamic_cast<PegModule*>(module);
		if (m) {
			if (PegWidget* w = findPegWidgetById(m->linkedAboveId)) {
				if (auto* nm = dynamic_cast<PegModule*>(w->module)) {
					if (nm->linkedBelowId == m->id) nm->linkedBelowId = -1;
				}
			}
			if (PegWidget* w = findPegWidgetById(m->linkedBelowId)) {
				if (auto* nm = dynamic_cast<PegModule*>(w->module)) {
					if (nm->linkedAboveId == m->id) nm->linkedAboveId = -1;
				}
			}
			if (PegWidget* w = findPegWidgetById(m->linkedLeftId)) {
				if (auto* nm = dynamic_cast<PegModule*>(w->module)) {
					if (nm->linkedRightId == m->id) nm->linkedRightId = -1;
				}
			}
			if (PegWidget* w = findPegWidgetById(m->linkedRightId)) {
				if (auto* nm = dynamic_cast<PegModule*>(w->module)) {
					if (nm->linkedLeftId == m->id) nm->linkedLeftId = -1;
				}
			}
		}
	}
	HarnessOverlayWidget::release(this);
}

void PegWidget::step() {
	PegModule* m = dynamic_cast<PegModule*>(module);
	if (m && APP && APP->scene && APP->scene->rack) {
		// Ensure shared overlay is attached
		HarnessOverlayWidget::acquire(this);

		// 1. Check vertical neighbors directly above & below
		PegWidget* aboveNeighbor = findVerticallyAlignedNeighbor(this, true);
		PegWidget* belowNeighbor = findVerticallyAlignedNeighbor(this, false);

		// Clear manual unlink state if the neighbor changed or moved away
		if (!aboveNeighbor || aboveNeighbor->module->id != m->manualUnlinkAboveId) {
			m->manualUnlinkAboveId = -1;
		}
		if (!belowNeighbor || belowNeighbor->module->id != m->manualUnlinkBelowId) {
			m->manualUnlinkBelowId = -1;
		}

		// Validate linkedAboveId (auto-break if moved out of vertical alignment or deleted)
		if (m->linkedAboveId != -1) {
			if (!aboveNeighbor || aboveNeighbor->module->id != m->linkedAboveId) {
				if (PegWidget* oldW = findPegWidgetById(m->linkedAboveId)) {
					if (auto* oldM = dynamic_cast<PegModule*>(oldW->module)) {
						if (oldM->linkedBelowId == m->id) oldM->linkedBelowId = -1;
					}
				}
				m->linkedAboveId = -1;
			}
			else {
				// Ensure reciprocity
				if (auto* aboveM = dynamic_cast<PegModule*>(aboveNeighbor->module)) {
					aboveM->linkedBelowId = m->id;
				}
			}
		}
		else if (aboveNeighbor && aboveNeighbor->module) {
			// Auto-link by default when vertically aligned unless manually unlinked!
			auto* aboveM = dynamic_cast<PegModule*>(aboveNeighbor->module);
			if (aboveM && m->manualUnlinkAboveId != aboveM->id && aboveM->manualUnlinkBelowId != m->id) {
				m->linkedAboveId = aboveM->id;
				aboveM->linkedBelowId = m->id;
			}
		}

		// Validate linkedBelowId
		if (m->linkedBelowId != -1) {
			if (!belowNeighbor || belowNeighbor->module->id != m->linkedBelowId) {
				if (PegWidget* oldW = findPegWidgetById(m->linkedBelowId)) {
					if (auto* oldM = dynamic_cast<PegModule*>(oldW->module)) {
						if (oldM->linkedAboveId == m->id) oldM->linkedAboveId = -1;
					}
				}
				m->linkedBelowId = -1;
			}
			else {
				if (auto* belowM = dynamic_cast<PegModule*>(belowNeighbor->module)) {
					belowM->linkedAboveId = m->id;
				}
			}
		}
		else if (belowNeighbor && belowNeighbor->module) {
			// Auto-link by default when vertically aligned unless manually unlinked!
			auto* belowM = dynamic_cast<PegModule*>(belowNeighbor->module);
			if (belowM && m->manualUnlinkBelowId != belowM->id && belowM->manualUnlinkAboveId != m->id) {
				m->linkedBelowId = belowM->id;
				belowM->linkedAboveId = m->id;
			}
		}

		// Show vertical link buttons whenever aligned with a vertical neighbor,
		// and automatically reposition them so they never collide with the PEGS header or 2HP footer when unlinked!
		if (topLinkBtn) {
			topLinkBtn->visible = (aboveNeighbor != nullptr);
			topLinkBtn->box.pos.y = (m->linkedAboveId != -1) ? 24.0f : 43.2f;
		}
		if (botLinkBtn) {
			botLinkBtn->visible = (belowNeighbor != nullptr);
			botLinkBtn->box.pos.y = (m->linkedBelowId != -1) ? 344.5f : 335.5f;
		}

		// 2. Validate horizontal same-row links (linkedLeftId & linkedRightId)
		if (m->linkedLeftId != -1) {
			PegWidget* leftW = findPegWidgetById(m->linkedLeftId);
			if (!leftW || std::fabs(leftW->box.pos.y - box.pos.y) > 1.5f || leftW->box.pos.x >= box.pos.x - 1.5f) {
				if (leftW) {
					if (auto* leftM = dynamic_cast<PegModule*>(leftW->module)) {
						if (leftM->linkedRightId == m->id) leftM->linkedRightId = -1;
					}
				}
				m->linkedLeftId = -1;
			}
			else {
				if (auto* leftM = dynamic_cast<PegModule*>(leftW->module)) {
					leftM->linkedRightId = m->id;
				}
			}
		}

		if (m->linkedRightId != -1) {
			PegWidget* rightW = findPegWidgetById(m->linkedRightId);
			if (!rightW || std::fabs(rightW->box.pos.y - box.pos.y) > 1.5f || rightW->box.pos.x <= box.pos.x + 1.5f) {
				if (rightW) {
					if (auto* rightM = dynamic_cast<PegModule*>(rightW->module)) {
						if (rightM->linkedLeftId == m->id) rightM->linkedLeftId = -1;
					}
				}
				m->linkedRightId = -1;
			}
			else {
				if (auto* rightM = dynamic_cast<PegModule*>(rightW->module)) {
					rightM->linkedLeftId = m->id;
				}
			}
		}
	}

	ModuleWidget::step();
}

void PegWidget::appendContextMenu(ui::Menu* menu) {
	ModuleWidget::appendContextMenu(menu);
	PegModule* m = dynamic_cast<PegModule*>(module);
	if (!m) return;

	PegWidget* leftNeighbor = findNearestHorizontalNeighbor(this, true);
	PegWidget* rightNeighbor = findNearestHorizontalNeighbor(this, false);

	menu->addChild(new ui::MenuSeparator);
	menu->addChild(createMenuLabel("Horizontal Keep-Out Linking"));

	// Button 1: Link to Left Peg Module
	if (leftNeighbor && leftNeighbor->module) {
		int64_t leftId = leftNeighbor->module->id;
		bool isLinkedLeft = (m->linkedLeftId == leftId);
		menu->addChild(createCheckMenuItem(
			"Link to Left Peg Module",
			isLinkedLeft ? "Linked" : "",
			[m, leftId]() { return m->linkedLeftId == leftId; },
			[this, m, leftId]() {
				if (m->linkedLeftId == leftId) {
					// Unlink
					if (PegWidget* lw = findPegWidgetById(leftId)) {
						if (auto* lm = dynamic_cast<PegModule*>(lw->module)) {
							if (lm->linkedRightId == m->id) lm->linkedRightId = -1;
						}
					}
					m->linkedLeftId = -1;
				}
				else {
					// Clear any previous left link first
					if (m->linkedLeftId != -1) {
						if (PegWidget* oldLw = findPegWidgetById(m->linkedLeftId)) {
							if (auto* oldLm = dynamic_cast<PegModule*>(oldLw->module)) {
								if (oldLm->linkedRightId == m->id) oldLm->linkedRightId = -1;
							}
						}
					}
					if (PegWidget* lw = findPegWidgetById(leftId)) {
						if (auto* lm = dynamic_cast<PegModule*>(lw->module)) {
							if (lm->linkedRightId != -1) {
								if (PegWidget* prevRw = findPegWidgetById(lm->linkedRightId)) {
									if (auto* prevRm = dynamic_cast<PegModule*>(prevRw->module)) {
										if (prevRm->linkedLeftId == leftId) prevRm->linkedLeftId = -1;
									}
								}
							}
							lm->linkedRightId = m->id;
						}
					}
					m->linkedLeftId = leftId;
				}
			}
		));
	}
	else {
		ui::MenuItem* disabledLeft = createMenuItem("Link to Left Peg Module", "None on row");
		disabledLeft->disabled = true;
		menu->addChild(disabledLeft);
	}

	// Button 2: Link to Right Peg Module
	if (rightNeighbor && rightNeighbor->module) {
		int64_t rightId = rightNeighbor->module->id;
		bool isLinkedRight = (m->linkedRightId == rightId);
		menu->addChild(createCheckMenuItem(
			"Link to Right Peg Module",
			isLinkedRight ? "Linked" : "",
			[m, rightId]() { return m->linkedRightId == rightId; },
			[this, m, rightId]() {
				if (m->linkedRightId == rightId) {
					// Unlink
					if (PegWidget* rw = findPegWidgetById(rightId)) {
						if (auto* rm = dynamic_cast<PegModule*>(rw->module)) {
							if (rm->linkedLeftId == m->id) rm->linkedLeftId = -1;
						}
					}
					m->linkedRightId = -1;
				}
				else {
					if (m->linkedRightId != -1) {
						if (PegWidget* oldRw = findPegWidgetById(m->linkedRightId)) {
							if (auto* oldRm = dynamic_cast<PegModule*>(oldRw->module)) {
								if (oldRm->linkedLeftId == m->id) oldRm->linkedLeftId = -1;
							}
						}
					}
					if (PegWidget* rw = findPegWidgetById(rightId)) {
						if (auto* rm = dynamic_cast<PegModule*>(rw->module)) {
							if (rm->linkedLeftId != -1) {
								if (PegWidget* prevLw = findPegWidgetById(rm->linkedLeftId)) {
									if (auto* prevLm = dynamic_cast<PegModule*>(prevLw->module)) {
										if (prevLm->linkedRightId == rightId) prevLm->linkedRightId = -1;
									}
								}
							}
							rm->linkedLeftId = m->id;
						}
					}
					m->linkedRightId = rightId;
				}
			}
		));
	}
	else {
		ui::MenuItem* disabledRight = createMenuItem("Link to Right Peg Module", "None on row");
		disabledRight->disabled = true;
		menu->addChild(disabledRight);
	}

	// Also expose vertical link options in right-click menu if vertically aligned
	PegWidget* aboveNeighbor = findVerticallyAlignedNeighbor(this, true);
	PegWidget* belowNeighbor = findVerticallyAlignedNeighbor(this, false);
	if (aboveNeighbor || belowNeighbor) {
		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Vertical Keep-Out Linking"));
		if (aboveNeighbor && aboveNeighbor->module) {
			int64_t aboveId = aboveNeighbor->module->id;
			menu->addChild(createCheckMenuItem(
				"Link to Peg Module Above",
				"",
				[m, aboveId]() { return m->linkedAboveId == aboveId; },
				[m, aboveId]() {
					if (m->linkedAboveId == aboveId) {
						if (PegWidget* aw = findPegWidgetById(aboveId)) {
							if (auto* am = dynamic_cast<PegModule*>(aw->module)) {
								if (am->linkedBelowId == m->id) am->linkedBelowId = -1;
								am->manualUnlinkBelowId = m->id;
							}
						}
						m->linkedAboveId = -1;
						m->manualUnlinkAboveId = aboveId;
					}
					else {
						if (PegWidget* aw = findPegWidgetById(aboveId)) {
							if (auto* am = dynamic_cast<PegModule*>(aw->module)) {
								am->linkedBelowId = m->id;
								am->manualUnlinkBelowId = -1;
							}
						}
						m->linkedAboveId = aboveId;
						m->manualUnlinkAboveId = -1;
					}
				}
			));
		}
		if (belowNeighbor && belowNeighbor->module) {
			int64_t belowId = belowNeighbor->module->id;
			menu->addChild(createCheckMenuItem(
				"Link to Peg Module Below",
				"",
				[m, belowId]() { return m->linkedBelowId == belowId; },
				[m, belowId]() {
					if (m->linkedBelowId == belowId) {
						if (PegWidget* bw = findPegWidgetById(belowId)) {
							if (auto* bm = dynamic_cast<PegModule*>(bw->module)) {
								if (bm->linkedAboveId == m->id) bm->linkedAboveId = -1;
								bm->manualUnlinkAboveId = m->id;
							}
						}
						m->linkedBelowId = -1;
						m->manualUnlinkBelowId = belowId;
					}
					else {
						if (PegWidget* bw = findPegWidgetById(belowId)) {
							if (auto* bm = dynamic_cast<PegModule*>(bw->module)) {
								bm->linkedAboveId = m->id;
								bm->manualUnlinkAboveId = -1;
							}
						}
						m->linkedBelowId = belowId;
						m->manualUnlinkBelowId = -1;
					}
				}
			));
		}
	}
}

Model* modelCablePegs = createModel<PegModule, PegWidget>("CablePegs");

} // namespace ZipTies
