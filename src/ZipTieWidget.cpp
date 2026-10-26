#include "plugin.hpp"
#include "ZipTieModule.hpp"
#include "HarnessOverlayWidget.hpp"
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <cmath>

namespace ZipTies {

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
		float ty = 6.0f;
		float tw = w - 8.0f;
		float th = h - 12.0f;
		int teeth = 7;
		float toothH = th / (float)teeth;
		float jag = 2.6f;

		// Helper lambda to trace serrated Scotch tape path
		auto traceTapePath = [&](float offsetX, float offsetY) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, tx + offsetX, ty + offsetY);
			// Top edge (slight organic bow)
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
			tieStyle = (int)module->params[ZipTieModule::PARAM_TIE_STYLE].getValue();
		}

		// Map 2.0..6.0 px parameter to a clear visual spread inside the preview bay
		float previewPitch = 2.6f + (rawSpacing - 2.0f) * 1.95f; // 2.6px at min, 10.4px at max

		float leftHoleX = 9.5f;
		float rightHoleX = w - 9.5f;
		float midX = w * 0.5f;
		float holeRx = 4.8f;
		float holeRy = 13.5f;

		// 1. Draw recessed panel pass-through grommet holes (left & right)
		auto drawPanelGrommetBack = [&](float hx, float hy) {
			// Machined aluminum bevel around hole
			nvgBeginPath(vg);
			nvgEllipse(vg, hx, hy, holeRx + 1.6f, holeRy + 1.6f);
			nvgFillColor(vg, nvgRGB(42, 46, 50));
			nvgFill(vg);
			nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 130));
			nvgStrokeWidth(vg, 0.7f);
			nvgStroke(vg);

			// Dark interior cavity under the panel
			nvgBeginPath(vg);
			nvgEllipse(vg, hx, hy, holeRx, holeRy);
			nvgFillColor(vg, nvgRGB(10, 12, 14));
			nvgFill(vg);
		};

		drawPanelGrommetBack(leftHoleX, cy);
		drawPanelGrommetBack(rightHoleX, cy);

		// 2. Define the 3 fake wires: Red, Blue, Gold
		NVGcolor wireColors[3] = {
			nvgRGB(209, 44, 38),  // Classic red wire
			nvgRGB(36, 114, 200), // Classic blue wire
			nvgRGB(224, 162, 28)  // Classic amber/yellow wire
		};

		// Offsets: index 0 = top (-1), index 1 = middle (0), index 2 = bottom (+1)
		float indices[3] = { -1.0f, 0.0f, 1.0f };

		nvgLineCap(vg, NVG_ROUND);
		nvgLineJoin(vg, NVG_ROUND);

		// 3. Pass 1: Drop shadows of all 3 wires on the aluminum panel
		for (int i = 0; i < 3; ++i) {
			float idx = indices[i];
			float yEnds = cy + idx * 3.8f;
			float yMid = cy + idx * previewPitch;

			nvgBeginPath(vg);
			nvgMoveTo(vg, leftHoleX - 1.0f, yEnds + 2.5f);
			nvgBezierTo(
				vg,
				leftHoleX + 13.0f, yMid + 2.5f,
				midX - 11.0f, yMid + 2.5f,
				midX, yMid + 2.5f
			);
			nvgBezierTo(
				vg,
				midX + 11.0f, yMid + 2.5f,
				rightHoleX - 13.0f, yMid + 2.5f,
				rightHoleX + 1.0f, yEnds + 2.5f
			);
			nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 75));
			nvgStrokeWidth(vg, 4.4f);
			nvgStroke(vg);
		}

		// 4. Pass 2: Draw the 3 wire bodies emerging from left hole, across, and into right hole
		for (int i = 0; i < 3; ++i) {
			float idx = indices[i];
			float yEnds = cy + idx * 3.8f;
			float yMid = cy + idx * previewPitch;
			NVGcolor col = wireColors[i];

			// Wire path
			auto traceWire = [&]() {
				nvgBeginPath(vg);
				nvgMoveTo(vg, leftHoleX - 1.2f, yEnds);
				nvgBezierTo(
					vg,
					leftHoleX + 13.0f, yMid,
					midX - 11.0f, yMid,
					midX, yMid
				);
				nvgBezierTo(
					vg,
					midX + 11.0f, yMid,
					rightHoleX - 13.0f, yMid,
					rightHoleX + 1.2f, yEnds
				);
			};

			// Outer darker insulation jacket
			traceWire();
			nvgStrokeColor(vg, color::mult(col, 0.72f));
			nvgStrokeWidth(vg, 4.2f);
			nvgStroke(vg);

			// Inner bright insulation core
			traceWire();
			nvgStrokeColor(vg, col);
			nvgStrokeWidth(vg, 3.1f);
			nvgStroke(vg);

			// Specular highlight along wire
			traceWire();
			nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 65));
			nvgStrokeWidth(vg, 1.0f);
			nvgStroke(vg);
		}

		// 5. Draw grommet outer lips overlapping the extreme outer edges so wires clearly tuck UNDER the panel
		auto drawGrommetOuterShadowClip = [&](float hx, float hy, bool isLeft) {
			nvgSave(vg);
			nvgBeginPath(vg);
			nvgEllipse(vg, hx, hy, holeRx, holeRy);
			// Inner cavity shadow gradient where wires dive into darkness
			NVGpaint holeShade = nvgLinearGradient(
				vg,
				isLeft ? hx - holeRx : hx + holeRx, hy,
				isLeft ? hx + holeRx : hx - holeRx, hy,
				nvgRGBA(0, 0, 0, 220),
				nvgRGBA(0, 0, 0, 0)
			);
			nvgFillPaint(vg, holeShade);
			nvgFill(vg);

			// Crisp rubber grommet ring
			nvgBeginPath(vg);
			nvgEllipse(vg, hx, hy, holeRx + 0.8f, holeRy + 0.8f);
			nvgStrokeColor(vg, nvgRGB(22, 25, 28));
			nvgStrokeWidth(vg, 1.6f);
			nvgStroke(vg);
			nvgRestore(vg);
		};

		drawGrommetOuterShadowClip(leftHoleX, cy, true);
		drawGrommetOuterShadowClip(rightHoleX, cy, false);

		// 6. Draw Miniature Zip Tie cinching the 3 preview wires at midX
		float bundleSpan = 2.0f * previewPitch;
		float tieH = bundleSpan + 9.0f;
		float tieW = 5.5f;
		float tieX = midX - tieW * 0.5f;
		float tieY = cy - tieH * 0.5f;

		NVGcolor tieBodyCol = (tieStyle == 1)
			? nvgRGB(28, 30, 34)
			: nvgRGBA(235, 238, 242, 215);

		// Mini zip tie shadow
		nvgBeginPath(vg);
		nvgRoundedRect(vg, tieX + 0.8f, tieY + 1.2f, tieW, tieH, 2.0f);
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 80));
		nvgFill(vg);

		// Mini zip tie band
		nvgBeginPath(vg);
		nvgRoundedRect(vg, tieX, tieY, tieW, tieH, 2.0f);
		nvgFillColor(vg, tieBodyCol);
		nvgFill(vg);
		nvgStrokeColor(vg, (tieStyle == 1) ? nvgRGBA(255, 255, 255, 55) : nvgRGBA(255, 255, 255, 140));
		nvgStrokeWidth(vg, 0.8f);
		nvgStroke(vg);

		// Mini zip tie buckle head at top of bundle
		float headW = tieW + 2.0f;
		float headH = 4.2f;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, midX - headW * 0.5f, tieY - 1.5f, headW, headH, 1.2f);
		nvgFillColor(vg, color::mult(tieBodyCol, 0.85f));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 90));
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

		// 1. Recessed armored switch well (inside the SVG hazard border)
		float wellX = 4.0f;
		float wellY = 4.0f;
		float wellW = w - 8.0f;
		float wellH = h - 8.0f;

		nvgBeginPath(vg);
		nvgRoundedRect(vg, wellX, wellY, wellW, wellH, 2.0f);
		nvgFillColor(vg, nvgRGB(16, 18, 20));
		nvgFill(vg);
		nvgStrokeColor(vg, nvgRGB(8, 9, 10));
		nvgStrokeWidth(vg, 1.2f);
		nvgStroke(vg);

		// Metallic mounting collar around the round danger button
		float btnR = 13.5f;
		nvgBeginPath(vg);
		nvgCircle(vg, cx, cy + 1.0f, btnR + 2.8f);
		nvgFillColor(vg, nvgRGB(55, 60, 66));
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

		// Warning triangle / text on the red button itself
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
		setPanel(createPanel(asset::plugin(pluginInstance, "res/ZipTie.svg")));

		// Screws (industrial silver screws matching NIM-BIN faceplate)
		addChild(createWidget<ScrewSilver>(math::Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(math::Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(math::Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(math::Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

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
		previewWires->box.pos = math::Vec(7, 176);
		previewWires->box.size = math::Vec(76, 44);
		addChild(previewWires);

		// 4. Spacing Knob (defaults all the way up at 6.0f)
		addParam(createParamCentered<NimInstrumentKnob>(
			math::Vec(45, 250), module, ZipTieModule::PARAM_WIRE_SPACING));

		// 5. Dangerous Delete-All Button with Flip-Up Molly-Guard (2 clicks < 1s)
		MollyGuardSnipButton* mollyBtn = new MollyGuardSnipButton;
		mollyBtn->module = module;
		mollyBtn->box.pos = math::Vec(16, 296);
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
