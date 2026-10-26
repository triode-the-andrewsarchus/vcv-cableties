#include "plugin.hpp"
#include "ZipTieModule.hpp"
#include "HarnessOverlayWidget.hpp"
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <cmath>

namespace ZipTies {

// ============================================================================
// 0. NIM-BIN PANEL TYPOGRAPHY & HAZARD STRIPES OVERLAY
//    (Drawn via NanoVG because VCV's NanoSVG ignores <text> and <pattern>)
// ============================================================================
struct NimPanelGraphicsWidget : widget::TransparentWidget {
	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		if (!APP || !APP->window || !APP->window->uiFont) return;

		bool dark = settings::preferDarkPanels;

		nvgSave(vg);
		nvgFontFaceId(vg, APP->window->uiFont->handle);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

		float cx = box.size.x * 0.5f;

		// 1. Top NIM-BIN Header
		nvgFontSize(vg, 12.5f);
		nvgTextLetterSpacing(vg, 1.5f);
		nvgFillColor(vg, dark ? nvgRGB(236, 240, 246) : nvgRGB(26, 30, 34));
		nvgText(vg, cx + 0.5f, 25.0f, "ZIP-TIE", NULL);

		// Terracotta subtitle badge ("Wire Harness")
		const char* badgeText = "Wire Harness";
		nvgFontSize(vg, 7.0f);
		nvgTextLetterSpacing(vg, 0.5f);
		float badgeBounds[4] = {0, 0, 0, 0};
		nvgTextBounds(vg, 0, 0, badgeText, NULL, badgeBounds);
		float badgeW = std::max(54.0f, (badgeBounds[2] - badgeBounds[0]) + 10.0f);

		nvgBeginPath(vg);
		nvgRoundedRect(vg, cx - badgeW * 0.5f, 32.2f, badgeW, 9.2f, 1.5f);
		nvgFillColor(vg, dark ? nvgRGB(200, 68, 36) : nvgRGB(184, 58, 27));
		nvgFill(vg);

		nvgFillColor(vg, nvgRGB(246, 245, 240));
		nvgText(vg, cx + 0.2f, 37.0f, badgeText, NULL);

		// 2. Section 1 Header: Shortcut Tape
		nvgFontSize(vg, 6.8f);
		nvgTextLetterSpacing(vg, 1.0f);
		nvgFillColor(vg, dark ? nvgRGB(180, 186, 195) : nvgRGB(38, 42, 46));
		nvgText(vg, cx + 0.4f, 51.5f, "KEY SHORTCUT", NULL);

		// 3. Section 2 Header: Rectangular Tie Applicator
		nvgFontSize(vg, 7.2f);
		nvgTextLetterSpacing(vg, 0.9f);
		nvgFillColor(vg, dark ? nvgRGB(210, 216, 225) : nvgRGB(28, 32, 36));
		nvgText(vg, cx + 0.4f, 106.5f, "TIE APPLICATOR", NULL);

		nvgFontSize(vg, 6.0f);
		nvgTextLetterSpacing(vg, 0.8f);
		nvgFillColor(vg, dark ? nvgRGB(224, 86, 52) : nvgRGB(184, 58, 27));
		nvgText(vg, cx + 0.4f, 115.0f, "MANUAL ARM", NULL);

		// 4. Section 3 Header: Harness Pitch & Scale Labels
		nvgFontSize(vg, 7.2f);
		nvgTextLetterSpacing(vg, 0.9f);
		nvgFillColor(vg, dark ? nvgRGB(210, 216, 225) : nvgRGB(28, 32, 36));
		nvgText(vg, cx + 0.4f, 168.5f, "HARNESS PITCH", NULL);

		// MIN / MAX calibration labels around knob
		nvgFontSize(vg, 6.5f);
		nvgTextLetterSpacing(vg, 0.4f);
		nvgFillColor(vg, dark ? nvgRGB(175, 182, 192) : nvgRGB(38, 42, 46));
		nvgText(vg, 20.0f, 272.5f, "MIN", NULL);
		nvgFillColor(vg, dark ? nvgRGB(224, 86, 52) : nvgRGB(184, 58, 27));
		nvgText(vg, 70.0f, 272.5f, "MAX", NULL);

		// 5. Section 4 Header: Danger Jettison All & Diagonal Hazard Stripes
		nvgFontSize(vg, 7.2f);
		nvgTextLetterSpacing(vg, 0.9f);
		nvgFillColor(vg, dark ? nvgRGB(235, 60, 48) : nvgRGB(180, 28, 20));
		nvgText(vg, cx + 0.4f, 288.5f, "JETTISON ALL", NULL);

		// Diagonal Yellow/Black Hazard Stripes Frame around Molly-Guard (x=16, y=295, w=58, h=46)
		float hx = 16.0f;
		float hy = 295.0f;
		float hw = 58.0f;
		float hh = 46.0f;

		nvgSave(vg);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, hx, hy, hw, hh, 2.5f);
		nvgFillColor(vg, nvgRGB(224, 162, 24)); // Hazard amber-yellow base
		nvgFill(vg);

		// Clip diagonal black stripes inside hazard box
		nvgIntersectScissor(vg, hx, hy, hw, hh);
		nvgStrokeColor(vg, nvgRGB(26, 29, 32));
		nvgStrokeWidth(vg, 4.2f);
		nvgLineCap(vg, NVG_BUTT);
		for (float s = -hh; s < hw + hh; s += 8.4f) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, hx + s, hy + hh + 2.0f);
			nvgLineTo(vg, hx + s + hh + 4.0f, hy - 2.0f);
			nvgStroke(vg);
		}
		nvgRestore(vg);

		// Outer border around hazard box
		nvgBeginPath(vg);
		nvgRoundedRect(vg, hx, hy, hw, hh, 2.5f);
		nvgStrokeColor(vg, dark ? nvgRGB(40, 44, 49) : nvgRGB(26, 29, 32));
		nvgStrokeWidth(vg, 1.0f);
		nvgStroke(vg);

		// Subtitle below Molly-Guard
		nvgFontSize(vg, 5.8f);
		nvgTextLetterSpacing(vg, 0.5f);
		nvgFillColor(vg, dark ? nvgRGB(160, 168, 178) : nvgRGB(48, 52, 56));
		nvgText(vg, cx + 0.3f, 346.8f, "LIFT GUARD · 2X CLICK", NULL);

		// 6. Bottom NIM-BIN Footer
		nvgFontSize(vg, 6.2f);
		nvgTextLetterSpacing(vg, 1.2f);
		nvgFillColor(vg, dark ? nvgRGB(145, 153, 164) : nvgRGB(42, 46, 50));
		nvgText(vg, cx + 0.5f, 358.0f, "NIM-BIN · STD", NULL);

		nvgRestore(vg);
	}
};

// ============================================================================
// 1. SCOTCH TAPE + SHARPIE SHORTCUT DISPLAY
// ============================================================================
struct ScotchTapeWidget : widget::TransparentWidget {
	ZipTieModule* module = nullptr;

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgSave(vg);

		float w = box.size.x;
		float h = box.size.y;
		float cx = w * 0.5f;
		float cy = h * 0.5f;

		// Slight natural hand-applied tilt (-2.2 degrees)
		nvgTranslate(vg, cx, cy);
		nvgRotate(vg, -0.038f);
		nvgTranslate(vg, -cx, -cy);

		float tx = 4.0f;
		float ty = 5.0f;
		float tw = w - 8.0f;
		float th = h - 10.0f;
		int teeth = 7;
		float toothH = th / (float)teeth;
		float jag = 2.6f;

		// Helper lambda to trace serrated Scotch tape path
		auto traceTapePath = [&](float offsetX, float offsetY) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, tx + offsetX, ty + offsetY);
			// Top edge
			nvgQuadTo(vg, cx + offsetX, ty - 0.6f + offsetY, tx + tw + offsetX, ty + offsetY);
			// Right serrated torn edge
			for (int i = 0; i < teeth; ++i) {
				float yMid = ty + (i + 0.5f) * toothH + offsetY;
				float yBot = ty + (i + 1.0f) * toothH + offsetY;
				nvgLineTo(vg, tx + tw - jag + offsetX, yMid);
				nvgLineTo(vg, tx + tw + offsetX, yBot);
			}
			// Bottom edge
			nvgQuadTo(vg, cx + offsetX, ty + th + 0.6f + offsetY, tx + offsetX, ty + th + offsetY);
			// Left serrated torn edge (going up)
			for (int i = teeth - 1; i >= 0; --i) {
				float yMid = ty + (i + 0.5f) * toothH + offsetY;
				float yTop = ty + (float)i * toothH + offsetY;
				nvgLineTo(vg, tx + jag + offsetX, yMid);
				nvgLineTo(vg, tx + offsetX, yTop);
			}
			nvgClosePath(vg);
		};

		// Subtle drop shadow under tape
		traceTapePath(0.8f, 1.4f);
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 48));
		nvgFill(vg);

		// Main translucent matte Scotch tape body (warm yellowish-cream)
		traceTapePath(0.0f, 0.0f);
		NVGpaint tapePaint = nvgLinearGradient(
			vg, tx, ty, tx + tw, ty + th,
			nvgRGBA(248, 244, 220, 178),
			nvgRGBA(238, 233, 204, 162)
		);
		nvgFillPaint(vg, tapePaint);
		nvgFill(vg);

		// Slightly more opaque pressed adhesive zones near left & right ends
		nvgBeginPath(vg);
		nvgRect(vg, tx + 2.5f, ty + 1.5f, 9.0f, th - 3.0f);
		nvgRect(vg, tx + tw - 11.5f, ty + 1.5f, 9.0f, th - 3.0f);
		nvgFillColor(vg, nvgRGBA(255, 253, 240, 38));
		nvgFill(vg);

		// Diagonal plastic sheen / crinkle highlight lines
		nvgBeginPath(vg);
		nvgMoveTo(vg, tx + 14.0f, ty + th - 1.0f);
		nvgLineTo(vg, tx + 24.0f, ty + 1.0f);
		nvgMoveTo(vg, tx + tw - 22.0f, ty + th - 1.0f);
		nvgLineTo(vg, tx + tw - 13.0f, ty + 1.0f);
		nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 70));
		nvgStrokeWidth(vg, 1.2f);
		nvgStroke(vg);

		// Fine matte rim around tape
		traceTapePath(0.0f, 0.0f);
		nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 95));
		nvgStrokeWidth(vg, 0.65f);
		nvgStroke(vg);

		// Sharpie Marker Text
		std::string label = "Z";
		bool isLearning = false;
		if (module) {
			if (module->learningKey) {
				isLearning = true;
				if (module->pendingMods != 0) {
					label = formatKeyCombo(0, module->pendingMods) + "+...";
				}
				else {
					label = "PRESS KEY";
				}
			}
			else {
				label = module->getShortcutName();
			}
		}

		std::shared_ptr<window::Font> markerFont = APP->window->loadFont(
			asset::plugin(pluginInstance, "res/fonts/PermanentMarker-Regular.ttf"));
		if (markerFont && markerFont->handle >= 0) {
			nvgFontFaceId(vg, markerFont->handle);
		}
		else if (APP->window->uiFont) {
			nvgFontFaceId(vg, APP->window->uiFont->handle);
		}

		// Auto-fit font size to tape width
		float fontSize = 16.5f;
		nvgFontSize(vg, fontSize);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

		float bounds[4] = {0, 0, 0, 0};
		nvgTextBounds(vg, 0, 0, label.c_str(), NULL, bounds);
		float textW = bounds[2] - bounds[0];
		float maxTextW = tw - 12.0f;
		if (textW > maxTextW && textW > 0.0f) {
			fontSize = std::max(8.5f, fontSize * (maxTextW / textW));
			nvgFontSize(vg, fontSize);
		}

		// Slight Sharpie ink bleed halo
		NVGcolor inkColor = isLearning ? nvgRGBA(175, 28, 20, 238) : nvgRGBA(20, 22, 30, 238);
		NVGcolor bleedColor = isLearning ? nvgRGBA(175, 28, 20, 45) : nvgRGBA(20, 22, 30, 40);
		nvgFillColor(vg, bleedColor);
		nvgText(vg, cx + 0.35f, cy + 0.85f, label.c_str(), NULL);

		// Main Sharpie stroke
		nvgFillColor(vg, inkColor);
		nvgText(vg, cx, cy + 0.5f, label.c_str(), NULL);

		// Subtle Sharpie underline flourish if short key name
		if ((int)label.size() <= 3 && !isLearning) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, cx - 10.0f, cy + 9.0f);
			nvgQuadTo(vg, cx, cy + 8.0f, cx + 11.0f, cy + 9.3f);
			nvgStrokeColor(vg, nvgRGBA(20, 22, 30, 175));
			nvgStrokeWidth(vg, 1.3f);
			nvgLineCap(vg, NVG_ROUND);
			nvgStroke(vg);
		}

		nvgRestore(vg);
	}
};

// ============================================================================
// 2. RECTANGULAR TOP BUTTON WITH ZIP-TIE ICON
// ============================================================================
struct RectangularZipTieButton : widget::OpaqueWidget {
	ZipTieModule* module = nullptr;
	bool momentaryPressed = false;

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (e.action == GLFW_PRESS) {
				momentaryPressed = true;
				if (module) {
					module->toolArmed = !module->toolArmed;
				}
				e.consume(this);
				return;
			}
			else if (e.action == GLFW_RELEASE) {
				momentaryPressed = false;
				e.consume(this);
				return;
			}
		}
		OpaqueWidget::onButton(e);
	}

	void onDragEnd(const DragEndEvent& e) override {
		momentaryPressed = false;
		OpaqueWidget::onDragEnd(e);
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgSave(vg);

		float w = box.size.x;
		float h = box.size.y;
		bool armed = module ? module->toolArmed : false;
		float pressOffset = (armed || momentaryPressed) ? 1.0f : 0.0f;

		// Outer NIM dark-anodized machined bezel
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0.0f, 0.0f, w, h, 2.5f);
		nvgFillColor(vg, nvgRGB(24, 27, 30));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGB(12, 14, 16));
		nvgStrokeWidth(vg, 1.0f);
		nvgStroke(vg);

		// Bezel bottom highlight lip on aluminum panel
		nvgBeginPath(vg);
		nvgMoveTo(vg, 1.0f, h + 0.5f);
		nvgLineTo(vg, w - 1.0f, h + 0.5f);
		nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 140));
		nvgStrokeWidth(vg, 0.8f);
		nvgStroke(vg);

		// Inner recessed switch well
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 2.5f, 2.5f, w - 5.0f, h - 5.0f, 1.8f);
		nvgFillColor(vg, nvgRGB(10, 12, 14));
		nvgFill(vg);

		// Rectangular Switch Cap
		float capX = 4.0f;
		float capY = 3.5f + pressOffset;
		float capW = w - 8.0f;
		float capH = h - 8.0f;

		// Drop shadow of cap inside well
		if (!armed && !momentaryPressed) {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, capX, capY + 1.8f, capW, capH, 1.5f);
			nvgFillColor(vg, nvgRGBA(0, 0, 0, 160));
			nvgFill(vg);
		}

		// Cap body gradient (illuminated NIM amber-orange when armed, dark charcoal when disarmed)
		nvgBeginPath(vg);
		nvgRoundedRect(vg, capX, capY, capW, capH, 1.5f);
		if (armed) {
			NVGpaint litPaint = nvgLinearGradient(
				vg, capX, capY, capX, capY + capH,
				nvgRGB(235, 110, 36),
				nvgRGB(185, 62, 18)
			);
			nvgFillPaint(vg, litPaint);
		}
		else {
			NVGpaint darkPaint = nvgLinearGradient(
				vg, capX, capY, capX, capY + capH,
				nvgRGB(64, 70, 76),
				nvgRGB(40, 44, 49)
			);
			nvgFillPaint(vg, darkPaint);
		}
		nvgFill(vg);

		// Cap specular top edge & border
		nvgBeginPath(vg);
		nvgRoundedRect(vg, capX + 0.5f, capY + 0.5f, capW - 1.0f, capH - 1.0f, 1.2f);
		nvgStrokeColor(vg, armed ? nvgRGBA(255, 220, 160, 180) : nvgRGBA(255, 255, 255, 55));
		nvgStrokeWidth(vg, 0.8f);
		nvgStroke(vg);

		// --------------------------------------------------------------------
		// DRAW ZIP-TIE ICON ON RECTANGULAR BUTTON CAP
		// --------------------------------------------------------------------
		float cy = capY + capH * 0.5f;
		NVGcolor iconCol = armed ? nvgRGB(255, 250, 235) : nvgRGB(218, 223, 216);
		NVGcolor iconDark = armed ? nvgRGB(95, 28, 8) : nvgRGB(24, 27, 30);

		// 1. Zip Tie Ratchet Locking Head (left side)
		float headX = capX + 6.0f;
		float headY = cy - 5.5f;
		float headW = 9.0f;
		float headH = 11.0f;

		nvgBeginPath(vg);
		nvgRoundedRect(vg, headX, headY, headW, headH, 1.8f);
		nvgFillColor(vg, iconCol);
		nvgFill(vg);

		// Ratchet pawl slot inside head
		nvgBeginPath(vg);
		nvgRoundedRect(vg, headX + 2.2f, cy - 2.8f, 4.6f, 5.6f, 0.8f);
		nvgFillColor(vg, iconDark);
		nvgFill(vg);

		// Little ratchet tooth bar inside pawl slot
		nvgBeginPath(vg);
		nvgRect(vg, headX + 3.4f, cy - 1.5f, 1.8f, 3.0f);
		nvgFillColor(vg, iconCol);
		nvgFill(vg);

		// 2. Zip Tie Strap Body with Serrated Teeth (extending right from head)
		float strapX = headX + headW - 0.5f;
		float strapH = 5.4f;
		float strapY = cy - strapH * 0.5f;
		float strapLen = 24.5f;
		float tipLen = 5.5f;

		nvgBeginPath(vg);
		nvgMoveTo(vg, strapX, strapY);
		nvgLineTo(vg, strapX + strapLen, strapY);
		// Tapered tail tip
		nvgLineTo(vg, strapX + strapLen + tipLen, cy - 1.1f);
		nvgLineTo(vg, strapX + strapLen + tipLen, cy + 1.1f);
		nvgLineTo(vg, strapX + strapLen, strapY + strapH);
		nvgLineTo(vg, strapX, strapY + strapH);
		nvgClosePath(vg);
		nvgFillColor(vg, iconCol);
		nvgFill(vg);

		// Transverse serrated ratchet teeth along the strap
		nvgBeginPath(vg);
		for (float tx = strapX + 3.0f; tx < strapX + strapLen - 1.5f; tx += 2.6f) {
			nvgMoveTo(vg, tx, strapY + 0.9f);
			nvgLineTo(vg, tx, strapY + strapH - 0.9f);
		}
		nvgStrokeColor(vg, iconDark);
		nvgStrokeWidth(vg, 1.0f);
		nvgStroke(vg);

		nvgRestore(vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && module && module->toolArmed) {
			// Illuminated glow on Layer 1 when armed
			NVGcontext* vg = args.vg;
			nvgSave(vg);
			float cx = box.size.x * 0.5f;
			float cy = box.size.y * 0.5f;
			NVGpaint glow = nvgRadialGradient(
				vg, cx, cy, 4.0f, 32.0f,
				nvgRGBA(255, 130, 40, 90),
				nvgRGBA(255, 130, 40, 0)
			);
			nvgBeginPath(vg);
			nvgRect(vg, -10.0f, -10.0f, box.size.x + 20.0f, box.size.y + 20.0f);
			nvgFillPaint(vg, glow);
			nvgFill(vg);
			nvgRestore(vg);
		}
		OpaqueWidget::drawLayer(args, layer);
	}
};

// ============================================================================
// 3. 3 FAKE PREVIEW WIRES SNAKING OUT & BACK INTO PANEL
//    - At MAX (6.0): wires have NO gap and NO overlap (touch edge-to-edge)
//    - At MIN (2.0): wires ALMOST ENTIRELY overlap (matching real tied wires)
//    - Snug pass-through slots on left/right with zero excess black space
// ============================================================================
struct WirePreviewWidget : widget::TransparentWidget {
	ZipTieModule* module = nullptr;

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgSave(vg);

		float w = box.size.x;
		float h = box.size.y;
		float cy = h * 0.5f;

		// Read spacing parameter (2.0f .. 6.0f, default 6.0f)
		float rawSpacing = 6.0f;
		int tieStyle = 0;
		if (module) {
			rawSpacing = module->params[ZipTieModule::PARAM_WIRE_SPACING].getValue();
			tieStyle = (int)std::round(module->params[ZipTieModule::PARAM_TIE_STYLE].getValue());
		}

		// Wire thickness matches real Rack cables (6.0f outer, 5.0f inner)
		const float wireOuter = 5.8f;
		const float wireInner = 4.8f;
		const float wireRadius = wireOuter * 0.5f;

		// Normalize knob position t in [0.0 (MIN) .. 1.0 (MAX)]
		float t = math::clamp((rawSpacing - 2.0f) / 4.0f, 0.0f, 1.0f);

		// At MAX (t=1): pitch == 5.6f -> exact edge-to-edge contact (no gap, no overlap)
		// At MIN (t=0): pitch == 1.05f -> wires almost entirely overlap (~82% overlap)
		float pitch = 1.05f + t * (5.60f - 1.05f);

		float leftX = 11.0f;
		float rightX = w - 11.0f;
		float midX = w * 0.5f;

		// Slight S-bow / arch so the 3 wires visibly snake out from left, across, and back in on right
		float endBowY = 2.0f;
		float midBowY = -1.2f;

		// Offsets: index 0 = top (-1), index 1 = middle (0), index 2 = bottom (+1)
		float indices[3] = { -1.0f, 0.0f, 1.0f };

		// 1. Draw snug pass-through grommet slots on left and right that tightly hug the wires
		//    (so there is never any empty black void above or below the wires)
		float slotHalfSpan = pitch + wireRadius + 0.8f;
		float slotW = 5.4f;

		auto drawSnugPassThroughBack = [&](float sx, float sy) {
			// Machined aluminum outer bezel rim around the slot
			nvgBeginPath(vg);
			nvgRoundedRect(
				vg,
				sx - slotW * 0.5f - 1.2f,
				sy - slotHalfSpan - 1.2f,
				slotW + 2.4f,
				slotHalfSpan * 2.0f + 2.4f,
				(slotW + 2.4f) * 0.5f
			);
			nvgFillColor(vg, nvgRGB(62, 66, 70));
			nvgFill(vg);
			nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 140));
			nvgStrokeWidth(vg, 0.7f);
			nvgStroke(vg);

			// Snug dark rubber grommet opening (exact height of the wire bundle)
			nvgBeginPath(vg);
			nvgRoundedRect(
				vg,
				sx - slotW * 0.5f,
				sy - slotHalfSpan,
				slotW,
				slotHalfSpan * 2.0f,
				slotW * 0.5f
			);
			nvgFillColor(vg, nvgRGB(18, 20, 22));
			nvgFill(vg);
		};

		drawSnugPassThroughBack(leftX, cy + endBowY);
		drawSnugPassThroughBack(rightX, cy + endBowY);

		// 2. Define the 3 fake wires: Red, Blue, Gold (matching VCV cable palette)
		NVGcolor wireColors[3] = {
			nvgRGB(215, 46, 40),  // Red cable
			nvgRGB(38, 122, 212), // Blue cable
			nvgRGB(232, 170, 28)  // Yellow/Gold cable
		};

		nvgLineCap(vg, NVG_ROUND);
		nvgLineJoin(vg, NVG_ROUND);

		auto traceWirePath = [&](float idx, float dropY) {
			float yEnd = cy + endBowY + idx * pitch + dropY;
			float yMid = cy + midBowY + idx * pitch + dropY;

			nvgBeginPath(vg);
			nvgMoveTo(vg, leftX, yEnd);
			nvgBezierTo(
				vg,
				leftX + 11.0f, yMid,
				midX - 11.0f, yMid,
				midX, yMid
			);
			nvgBezierTo(
				vg,
				midX + 11.0f, yMid,
				rightX - 11.0f, yMid,
				rightX, yEnd
			);
		};

		// 3. Pass 1: Drop shadow cast by the snaking wire bundle onto the aluminum panel
		for (int i = 0; i < 3; ++i) {
			traceWirePath(indices[i], 2.4f);
			nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 65));
			nvgStrokeWidth(vg, wireOuter);
			nvgStroke(vg);
		}

		// 4. Pass 2: Draw each wire (matching HarnessOverlayWidget cable rendering: 0.8x outer, 0.95x inner)
		for (int i = 0; i < 3; ++i) {
			float idx = indices[i];
			NVGcolor col = wireColors[i];

			// Outer cable jacket (width = wireOuter)
			traceWirePath(idx, 0.0f);
			nvgStrokeColor(vg, color::mult(col, 0.78f));
			nvgStrokeWidth(vg, wireOuter);
			nvgStroke(vg);

			// Inner cable body (width = wireInner)
			traceWirePath(idx, 0.0f);
			nvgStrokeColor(vg, color::mult(col, 0.96f));
			nvgStrokeWidth(vg, wireInner);
			nvgStroke(vg);

			// Subtle cylindrical highlight along upper-center of wire
			traceWirePath(idx, -0.5f);
			nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 48));
			nvgStrokeWidth(vg, 1.1f);
			nvgStroke(vg);
		}

		// 5. Draw machined aluminum pass-through hoods over the outer edges of the left/right slots
		//    so the wires clearly emerge from UNDER the panel on the left and tuck UNDER on the right
		auto drawPassThroughHood = [&](float sx, float sy, bool isLeft) {
			float hoodX = isLeft ? (sx - slotW * 0.5f - 2.2f) : sx;
			float hoodW = slotW * 0.5f + 2.2f;
			float hoodY = sy - slotHalfSpan - 0.5f;
			float hoodH = slotHalfSpan * 2.0f + 1.0f;

			// Subtle shadow cast by panel lip onto emerging wires
			nvgBeginPath(vg);
			nvgRect(
				vg,
				isLeft ? sx - 1.0f : sx - 2.5f,
				hoodY + 1.0f,
				3.5f,
				hoodH - 2.0f
			);
			nvgFillColor(vg, nvgRGBA(0, 0, 0, 95));
			nvgFill(vg);

			// Brushed aluminum panel lip overlapping outer wire tips
			nvgBeginPath(vg);
			nvgRoundedRect(vg, hoodX, hoodY, hoodW, hoodH, 1.5f);
			nvgFillColor(vg, settings::preferDarkPanels ? nvgRGB(40, 44, 49) : nvgRGB(208, 211, 205));
			nvgFill(vg);

			// Crisp inner bevel edge where wires emerge
			float bevelX = isLeft ? (hoodX + hoodW) : hoodX;
			nvgBeginPath(vg);
			nvgMoveTo(vg, bevelX, hoodY + 0.5f);
			nvgLineTo(vg, bevelX, hoodY + hoodH - 0.5f);
			nvgStrokeColor(vg, settings::preferDarkPanels ? nvgRGB(80, 88, 98) : nvgRGB(42, 46, 50));
			nvgStrokeWidth(vg, 1.1f);
			nvgStroke(vg);
		};

		drawPassThroughHood(leftX, cy + endBowY, true);
		drawPassThroughHood(rightX, cy + endBowY, false);

		// 6. Draw Miniature Zip Tie cinching the 3 preview wires at midX
		float bundleCenterY = cy + midBowY;
		float tieH = 2.0f * pitch + wireOuter + 5.0f;
		float tieW = 5.5f;
		float tieX = midX - tieW * 0.5f;
		float tieY = bundleCenterY - tieH * 0.5f;

		bool isBlackTie = (tieStyle == 1);
		NVGcolor tieBodyCol = isBlackTie
			? nvgRGB(28, 30, 34)
			: nvgRGBA(235, 238, 242, 205);

		// Mini zip tie drop shadow
		nvgBeginPath(vg);
		nvgRoundedRect(vg, tieX + 1.0f, tieY + 1.5f, tieW, tieH, 2.0f);
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 65));
		nvgFill(vg);

		// Mini zip tie strap body
		nvgBeginPath(vg);
		nvgRoundedRect(vg, tieX, tieY, tieW, tieH, 2.0f);
		nvgFillColor(vg, tieBodyCol);
		nvgFill(vg);
		nvgStrokeColor(vg, isBlackTie ? nvgRGBA(255, 255, 255, 55) : nvgRGBA(255, 255, 255, 150));
		nvgStrokeWidth(vg, 0.85f);
		nvgStroke(vg);

		// Mini zip tie buckle head at top of bundle
		float headW = tieW + 1.8f;
		float headH = 4.0f;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, midX - headW * 0.5f, tieY - 1.2f, headW, headH, 1.2f);
		nvgFillColor(vg, color::mult(tieBodyCol, 0.85f));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 80));
		nvgStrokeWidth(vg, 0.8f);
		nvgStroke(vg);

		nvgRestore(vg);
	}
};

// ============================================================================
// 4. NIM-BIN FLUTED INSTRUMENT KNOB
// ============================================================================
struct NimInstrumentKnob : RoundBlackKnob {
	NimInstrumentKnob() {
		RoundBlackKnob();
	}

	void draw(const DrawArgs& args) override {
		// Draw standard SVG base knob first
		RoundBlackKnob::draw(args);

		// Add machined aluminum center insert cap & high-visibility pointer
		NVGcontext* vg = args.vg;
		nvgSave(vg);
		float cx = box.size.x * 0.5f;
		float cy = box.size.y * 0.5f;

		// Spun aluminum inner cap
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy, 8.5f);
		NVGpaint capGrad = nvgLinearGradient(
			vg, cx - 8.f, cy - 8.f, cx + 8.f, cy + 8.f,
			nvgRGB(220, 224, 218),
			nvgRGB(150, 155, 148)
		);
		nvgFillPaint(vg, capGrad);
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGB(40, 44, 48));
		nvgStrokeWidth(vg, 0.9f);
		nvgStroke(vg);

		// Compute angle from ParamQuantity
		float normVal = 1.0f;
		if (getParamQuantity()) {
			normVal = getParamQuantity()->getScaledValue();
		}
		float angle = math::rescale(normVal, 0.0f, 1.0f, minAngle, maxAngle);

		nvgTranslate(vg, cx, cy);
		nvgRotate(vg, angle);

		// High-contrast red-orange index line across cap
		nvgBeginPath(vg);
		nvgMoveTo(vg, 0.0f, 2.0f);
		nvgLineTo(vg, 0.0f, -13.5f);
		nvgStrokeColor(vg, nvgRGB(210, 58, 28));
		nvgStrokeWidth(vg, 1.8f);
		nvgLineCap(vg, NVG_ROUND);
		nvgStroke(vg);

		nvgRestore(vg);
	}
};

// ============================================================================
// 5. MOLLY-GUARD DANGER DELETE-ALL BUTTON (2 CLICKS WITHIN < 1.0 SECOND)
// ============================================================================
struct MollyGuardSnipButton : widget::OpaqueWidget {
	ZipTieModule* module = nullptr;
	bool guardOpen = false;
	double guardOpenTime = -10.0;
	double buttonPressTime = -10.0;
	static constexpr double GUARD_TIMEOUT = 1.0; // Must click twice within under 1 second

	void step() override {
		if (guardOpen) {
			double elapsed = system::getTime() - guardOpenTime;
			if (elapsed >= GUARD_TIMEOUT) {
				guardOpen = false;
			}
		}
		OpaqueWidget::step();
	}

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			double now = system::getTime();
			double elapsed = now - guardOpenTime;

			if (!guardOpen || elapsed >= GUARD_TIMEOUT) {
				// FIRST CLICK: Flip open the visual Molly-Guard!
				guardOpen = true;
				guardOpenTime = now;
			}
			else {
				// SECOND CLICK WITHIN < 1 SECOND: Press the danger button & snip all ties!
				buttonPressTime = now;
				if (module) {
					module->clearAllZipTies();
				}
				guardOpen = false;
			}
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgSave(vg);

		float w = box.size.x;
		float h = box.size.y;
		float cx = w * 0.5f;
		float cy = h * 0.5f;

		double now = system::getTime();
		double elapsed = now - guardOpenTime;
		if (guardOpen && elapsed >= GUARD_TIMEOUT) {
			guardOpen = false;
		}
		bool recentlyTriggered = (now - buttonPressTime) < 0.18;

		// 1. Recessed armored switch well (inside the hazard border drawn by NimPanelGraphicsWidget)
		float wellX = 4.0f;
		float wellY = 4.0f;
		float wellW = w - 8.0f;
		float wellH = h - 8.0f;

		nvgBeginPath(vg);
		nvgRoundedRect(vg, wellX, wellY, wellW, wellH, 2.0f);
		nvgFillColor(vg, nvgRGB(22, 25, 28));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGB(10, 12, 14));
		nvgStrokeWidth(vg, 1.2f);
		nvgStroke(vg);

		// Metallic mounting collar around the round danger button
		float btnR = 13.5f;
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy + 1.0f, btnR + 2.8f);
		nvgFillColor(vg, nvgRGB(64, 70, 76));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGB(20, 22, 25));
		nvgStrokeWidth(vg, 1.0f);
		nvgStroke(vg);

		// 2. The Red DANGER Button Underneath
		float pressOffset = recentlyTriggered ? 1.5f : 0.0f;

		// Button drop shadow
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy + 2.5f, btnR);
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 180));
		nvgFill(vg);

		// Red mushroom plunger cap
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy + pressOffset, btnR);
		NVGpaint redBtnPaint = nvgRadialGradient(
			vg, cx - 3.5f, cy - 3.5f + pressOffset, 2.0f, btnR,
			guardOpen ? nvgRGB(255, 70, 55) : nvgRGB(215, 38, 28),
			guardOpen ? nvgRGB(185, 18, 12) : nvgRGB(135, 16, 12)
		);
		nvgFillPaint(vg, redBtnPaint);
		nvgFill(vg);

		nvgStrokeColor(vg, guardOpen ? nvgRGB(255, 160, 140) : nvgRGB(90, 15, 12));
		nvgStrokeWidth(vg, 1.1f);
		nvgStroke(vg);

		// Warning text on the red button itself
		if (APP->window->uiFont) {
			nvgFontFaceId(vg, APP->window->uiFont->handle);
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFontSize(vg, 7.5f);
			nvgFillColor(vg, nvgRGBA(255, 245, 240, guardOpen ? 245 : 190));
			nvgText(vg, cx, cy + pressOffset - 2.5f, "DANGER", NULL);
			nvgFontSize(vg, 6.5f);
			nvgText(vg, cx, cy + pressOffset + 4.5f, guardOpen ? "SNIP!" : "PURGE", NULL);
		}

		// 3. If Guard is OPEN: draw countdown arc (< 1.0s) and flipped-up Molly-Guard cover above hinge
		if (guardOpen) {
			float remainingFrac = math::clamp((float)(1.0 - (elapsed / GUARD_TIMEOUT)), 0.0f, 1.0f);

			// Countdown ring around the exposed red button
			nvgBeginPath(vg);
			nvgArc(
				vg, cx, cy + 1.0f, btnR + 1.6f,
				-M_PI * 0.5f,
				-M_PI * 0.5f + remainingFrac * 2.0f * M_PI,
				NVG_CW
			);
			nvgStrokeColor(vg, nvgRGB(255, 210, 40));
			nvgStrokeWidth(vg, 2.0f);
			nvgStroke(vg);

			// Flipped-up Molly-Guard cover (standing open above the top hinge)
			float covX = wellX - 1.0f;
			float covW = wellW + 2.0f;
			float openCovY = -14.0f;
			float openCovH = 18.0f;

			// Shadow cast by raised cover
			nvgBeginPath(vg);
			nvgRoundedRect(vg, covX + 1.0f, openCovY + 2.0f, covW, openCovH, 2.0f);
			nvgFillColor(vg, nvgRGBA(0, 0, 0, 90));
			nvgFill(vg);

			// Foreshortened raised translucent red safety cover
			nvgBeginPath(vg);
			nvgRoundedRect(vg, covX, openCovY, covW, openCovH, 2.0f);
			NVGpaint openCoverPaint = nvgLinearGradient(
				vg, covX, openCovY, covX, openCovY + openCovH,
				nvgRGBA(230, 50, 35, 200),
				nvgRGBA(150, 22, 15, 215)
			);
			nvgFillPaint(vg, openCoverPaint);
			nvgFill(vg);
			nvgStrokeColor(vg, nvgRGBA(255, 190, 175, 210));
			nvgStrokeWidth(vg, 1.0f);
			nvgStroke(vg);

			// Top flip-lip handle on the raised cover
			nvgBeginPath(vg);
			nvgRoundedRect(vg, cx - 12.0f, openCovY - 2.5f, 24.0f, 4.0f, 1.0f);
			nvgFillColor(vg, nvgRGB(210, 40, 28));
			nvgFill(vg);
			nvgStrokeColor(vg, nvgRGB(40, 10, 8));
			nvgStrokeWidth(vg, 0.8f);
			nvgStroke(vg);
		}
		else {
			// 4. If Guard is CLOSED: Draw the translucent safety Molly-Guard shielding the button!
			float covX = wellX - 0.5f;
			float covY = wellY + 1.0f;
			float covW = wellW + 1.0f;
			float covH = wellH - 1.5f;

			// Drop shadow of closed guard
			nvgBeginPath(vg);
			nvgRoundedRect(vg, covX + 1.0f, covY + 1.5f, covW, covH, 2.5f);
			nvgFillColor(vg, nvgRGBA(0, 0, 0, 110));
			nvgFill(vg);

			// Translucent red/amber polycarbonate safety cover
			nvgBeginPath(vg);
			nvgRoundedRect(vg, covX, covY, covW, covH, 2.5f);
			NVGpaint closedCoverPaint = nvgLinearGradient(
				vg, covX, covY, covX + covW, covY + covH,
				nvgRGBA(215, 42, 28, 130),
				nvgRGBA(140, 18, 12, 150)
			);
			nvgFillPaint(vg, closedCoverPaint);
			nvgFill(vg);

			// Molded side reinforcement ribs on the Molly-Guard
			nvgBeginPath(vg);
			nvgRect(vg, covX + 1.5f, covY + 2.0f, 3.0f, covH - 4.0f);
			nvgRect(vg, covX + covW - 4.5f, covY + 2.0f, 3.0f, covH - 4.0f);
			nvgFillColor(vg, nvgRGBA(180, 25, 18, 95));
			nvgFill(vg);

			// Glossy plastic diagonal reflection across safety cover
			nvgBeginPath(vg);
			nvgMoveTo(vg, covX + 6.0f, covY + covH - 3.0f);
			nvgLineTo(vg, covX + 18.0f, covY + 3.0f);
			nvgLineTo(vg, covX + 26.0f, covY + 3.0f);
			nvgLineTo(vg, covX + 14.0f, covY + covH - 3.0f);
			nvgClosePath(vg);
			nvgFillColor(vg, nvgRGBA(255, 255, 255, 48));
			nvgFill(vg);

			// Cover outer rim
			nvgBeginPath(vg);
			nvgRoundedRect(vg, covX, covY, covW, covH, 2.5f);
			nvgStrokeColor(vg, nvgRGBA(255, 160, 145, 175));
			nvgStrokeWidth(vg, 1.0f);
			nvgStroke(vg);

			// Bottom finger-lift lip tab
			nvgBeginPath(vg);
			nvgRoundedRect(vg, cx - 13.0f, covY + covH - 3.0f, 26.0f, 4.5f, 1.2f);
			nvgFillColor(vg, nvgRGBA(230, 55, 38, 225));
			nvgFill(vg);
			nvgStrokeColor(vg, nvgRGBA(255, 200, 185, 180));
			nvgStrokeWidth(vg, 0.7f);
			nvgStroke(vg);

			// Stencil warning text on safety cover
			if (APP->window->uiFont) {
				nvgFontFaceId(vg, APP->window->uiFont->handle);
				nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFontSize(vg, 6.8f);
				nvgFillColor(vg, nvgRGBA(255, 235, 180, 215));
				nvgText(vg, cx, covY + 7.5f, "SAFETY GUARD", NULL);
			}
		}

		// 5. Top Heavy Metal Hinge Bar (common to open and closed states)
		nvgBeginPath(vg);
		nvgRoundedRect(vg, wellX + 3.0f, wellY - 2.0f, wellW - 6.0f, 4.5f, 1.2f);
		nvgFillColor(vg, nvgRGB(75, 80, 86));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGB(20, 22, 25));
		nvgStrokeWidth(vg, 0.8f);
		nvgStroke(vg);

		// Hinge pin knuckles
		nvgBeginPath(vg);
		nvgRect(vg, wellX + 8.0f, wellY - 1.5f, 5.0f, 3.5f);
		nvgRect(vg, wellX + wellW - 13.0f, wellY - 1.5f, 5.0f, 3.5f);
		nvgFillColor(vg, nvgRGB(130, 136, 142));
		nvgFill(vg);

		nvgRestore(vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && guardOpen) {
			// Menacing red glow when Molly-Guard is flipped open
			NVGcontext* vg = args.vg;
			nvgSave(vg);
			float cx = box.size.x * 0.5f;
			float cy = box.size.y * 0.5f;
			NVGpaint glow = nvgRadialGradient(
				vg, cx, cy, 4.0f, 28.0f,
				nvgRGBA(255, 45, 30, 115),
				nvgRGBA(255, 45, 30, 0)
			);
			nvgBeginPath(vg);
			nvgRect(vg, -8.0f, -8.0f, box.size.x + 16.0f, box.size.y + 16.0f);
			nvgFillPaint(vg, glow);
			nvgFill(vg);
			nvgRestore(vg);
		}
		OpaqueWidget::drawLayer(args, layer);
	}
};

// ============================================================================
// 6. RIGHT-CLICK MENU KEY BINDER ITEM
// ============================================================================
struct ShortcutBindMenuItem : ui::MenuItem {
	ZipTieModule* module = nullptr;
	HarnessOverlayWidget* overlay = nullptr;

	void step() override {
		if (module) {
			if (module->learningKey) {
				if (module->pendingMods != 0) {
					text = "Holding " + formatKeyCombo(0, module->pendingMods) + "+...";
					rightText = "Press key or release";
				}
				else {
					text = "Press any key or combo...";
					rightText = "Esc to cancel";
				}
			}
			else {
				text = "Bind Custom Key / Combo...";
				rightText = module->getShortcutName();
			}
		}
		ui::MenuItem::step();
	}

	void onAction(const ActionEvent& e) override {
		if (!module) return;
		module->learningKey = !module->learningKey;
		module->pendingMods = 0;
		if (module->learningKey && APP && APP->event) {
			APP->event->setSelectedWidget(this);
		}
		// Keep menu open so user can press their key combo right in the menu
		e.unconsume();
	}

	void onSelectKey(const SelectKeyEvent& e) override {
		if (overlay && overlay->handleKeyLearn(e.key, e.action, e.mods)) {
			e.consume(this);
			return;
		}
		ui::MenuItem::onSelectKey(e);
	}

	void onHoverKey(const HoverKeyEvent& e) override {
		if (overlay && overlay->handleKeyLearn(e.key, e.action, e.mods)) {
			e.consume(this);
			return;
		}
		ui::MenuItem::onHoverKey(e);
	}
};

// ============================================================================
// 7. MAIN MODULE WIDGET
// ============================================================================
struct ZipTieWidget : app::ModuleWidget {
	HarnessOverlayWidget* overlayWidget = nullptr;

	ZipTieWidget(ZipTieModule* module) {
		setModule(module);
		setPanel(createPanel(
			asset::plugin(pluginInstance, "res/ZipTie.svg"),
			asset::plugin(pluginInstance, "res/ZipTie_dark.svg")
		));

		// 0. NIM-BIN Panel Typography & Hazard Stripes Overlay
		NimPanelGraphicsWidget* panelGraphics = new NimPanelGraphicsWidget;
		panelGraphics->box.pos = math::Vec(0, 0);
		panelGraphics->box.size = box.size;
		addChild(panelGraphics);

		// Screws (silver on light panel, black on dark panel)
		addChild(createWidget<ThemedScrew>(math::Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ThemedScrew>(math::Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ThemedScrew>(math::Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ThemedScrew>(math::Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// 1. Fake Piece of Scotch Tape with Sharpie Shortcut Display
		ScotchTapeWidget* tapeWidget = new ScotchTapeWidget;
		tapeWidget->module = module;
		tapeWidget->box.pos = math::Vec(8, 56);
		tapeWidget->box.size = math::Vec(74, 38);
		addChild(tapeWidget);

		// 2. Rectangular Top Button with Zip-Tie Icon (Arm Tool)
		RectangularZipTieButton* armBtn = new RectangularZipTieButton;
		armBtn->module = module;
		armBtn->box.pos = math::Vec(16, 122);
		armBtn->box.size = math::Vec(58, 30);
		addChild(armBtn);

		// 3. 3 Fake Preview Wires Snaking Out & Back In Above the Spacing Knob
		WirePreviewWidget* previewWires = new WirePreviewWidget;
		previewWires->module = module;
		previewWires->box.pos = math::Vec(7, 175);
		previewWires->box.size = math::Vec(76, 44);
		addChild(previewWires);

		// 4. Spacing Knob (defaults all the way up at 6.0f)
		addParam(createParamCentered<NimInstrumentKnob>(
			math::Vec(45, 250), module, ZipTieModule::PARAM_WIRE_SPACING));

		// 5. Dangerous Delete-All Button with Flip-Up Molly-Guard (2 clicks < 1s)
		MollyGuardSnipButton* mollyBtn = new MollyGuardSnipButton;
		mollyBtn->module = module;
		mollyBtn->box.pos = math::Vec(16, 295);
		mollyBtn->box.size = math::Vec(58, 46);
		addChild(mollyBtn);

		// Setup Harness Overlay on Rack
		if (module && APP && APP->scene && APP->scene->rack) {
			overlayWidget = new HarnessOverlayWidget;
			overlayWidget->module = module;
			overlayWidget->ownerMw = this;
			APP->scene->rack->addChild(overlayWidget);

			// Ensure overlay is positioned right after cableContainer so it renders on top
			std::list<widget::Widget*>::iterator it;
			for (it = APP->scene->rack->children.begin(); it != APP->scene->rack->children.end(); ++it) {
				if (*it == APP->scene->rack->getCableContainer()) break;
			}
			if (it != APP->scene->rack->children.end()) {
				++it;
				APP->scene->rack->children.splice(it, APP->scene->rack->children, --APP->scene->rack->children.end());
			}
		}
	}

	~ZipTieWidget() override {
		if (overlayWidget && APP && APP->scene && APP->scene->rack) {
			APP->scene->rack->removeChild(overlayWidget);
			delete overlayWidget;
			overlayWidget = nullptr;
		}
	}

	void appendContextMenu(ui::Menu* menu) override {
		ModuleWidget::appendContextMenu(menu);
		ZipTieModule* m = dynamic_cast<ZipTieModule*>(module);
		if (!m) return;

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Keyboard Shortcut"));

		// Interactive Key / Key-Combo Binder
		ShortcutBindMenuItem* bindItem = new ShortcutBindMenuItem;
		bindItem->module = m;
		bindItem->overlay = overlayWidget;
		menu->addChild(bindItem);

		// Preset Shortcuts submenu
		menu->addChild(createSubmenuItem("Preset Shortcuts", m->getShortcutName(), [m](ui::Menu* sub) {
			sub->addChild(createCheckMenuItem("Hold 'Z' (Default)", "",
				[m]() { return m->shortcutKey == GLFW_KEY_Z && m->shortcutMods == 0; },
				[m]() { m->setShortcut(GLFW_KEY_Z, 0); }
			));
			sub->addChild(createCheckMenuItem("Hold 'T'", "",
				[m]() { return m->shortcutKey == GLFW_KEY_T && m->shortcutMods == 0; },
				[m]() { m->setShortcut(GLFW_KEY_T, 0); }
			));
			sub->addChild(createCheckMenuItem("Hold 'X'", "",
				[m]() { return m->shortcutKey == GLFW_KEY_X && m->shortcutMods == 0; },
				[m]() { m->setShortcut(GLFW_KEY_X, 0); }
			));
			sub->addChild(createCheckMenuItem("Hold Shift + 'Z'", "",
				[m]() { return m->shortcutKey == GLFW_KEY_Z && m->shortcutMods == GLFW_MOD_SHIFT; },
				[m]() { m->setShortcut(GLFW_KEY_Z, GLFW_MOD_SHIFT); }
			));
			sub->addChild(createCheckMenuItem("Hold " RACK_MOD_CTRL_NAME "+Shift", "",
				[m]() { return m->shortcutKey == 0 && m->shortcutMods == (RACK_MOD_CTRL | GLFW_MOD_SHIFT); },
				[m]() { m->setShortcut(0, RACK_MOD_CTRL | GLFW_MOD_SHIFT); }
			));
			sub->addChild(createCheckMenuItem("Hold Shift", "",
				[m]() { return m->shortcutKey == 0 && m->shortcutMods == GLFW_MOD_SHIFT; },
				[m]() { m->setShortcut(0, GLFW_MOD_SHIFT); }
			));
		}));

		menu->addChild(new ui::MenuSeparator);
		menu->addChild(createMenuLabel("Zip Tie Color"));

		menu->addChild(createCheckMenuItem("Translucent (Default)", "",
			[m]() { return (int)std::round(m->params[ZipTieModule::PARAM_TIE_STYLE].getValue()) == 0; },
			[m]() { m->params[ZipTieModule::PARAM_TIE_STYLE].setValue(0.f); }
		));
		menu->addChild(createCheckMenuItem("Black", "",
			[m]() { return (int)std::round(m->params[ZipTieModule::PARAM_TIE_STYLE].getValue()) == 1; },
			[m]() { m->params[ZipTieModule::PARAM_TIE_STYLE].setValue(1.f); }
		));

		menu->addChild(new ui::MenuSeparator);

		// Arm Tool Toggle
		menu->addChild(createMenuItem(
			m->toolArmed ? "Disarm Tie Tool" : "Arm Tie Tool (Draw anywhere)",
			"",
			[m]() {
				m->toolArmed = !m->toolArmed;
			}
		));

		// Snip All Action
		menu->addChild(createMenuItem(
			"Snip All Zip Ties",
			"",
			[m]() {
				m->clearAllZipTies();
			}
		));
	}
};

Model* modelZipTie = createModel<ZipTieModule, ZipTieWidget>("ZipTie");

} // namespace ZipTies
