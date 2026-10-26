#include "plugin.hpp"
#include "ZipTieModule.hpp"
#include "HarnessOverlayWidget.hpp"
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>

namespace ZipTies {

struct StatusDisplayWidget : widget::TransparentWidget {
	ZipTieModule* module = nullptr;

	void draw(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		nvgSave(vg);

		// Subtle glass inner gradient
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 4.0f);
		nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, box.size.y, nvgRGBAf(0.12f, 0.16f, 0.22f, 0.5f), nvgRGBAf(0.04f, 0.06f, 0.08f, 0.8f)));
		nvgFill(vg);

		if (!module) {
			nvgRestore(vg);
			return;
		}

		nvgFontFaceId(vg, APP->window->uiFont->handle);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);

		// Line 1: Mode
		nvgFontSize(vg, 8.5f);
		if (module->learningKey) {
			nvgFillColor(vg, nvgRGB(250, 204, 21)); // Amber
			nvgText(vg, box.size.x * 0.5f, 6.0f, "PRESS KEY COMBO", NULL);
		}
		else if (module->toolArmed) {
			nvgFillColor(vg, nvgRGB(56, 189, 248)); // Bright cyan
			nvgText(vg, box.size.x * 0.5f, 6.0f, "* TOOL ARMED *", NULL);
		}
		else {
			nvgFillColor(vg, nvgRGB(148, 163, 184)); // Muted slate
			nvgText(vg, box.size.x * 0.5f, 6.0f, "HARNESS READY", NULL);
		}

		// Line 2: Ties Count
		nvgFontSize(vg, 9.5f);
		nvgFillColor(vg, nvgRGB(241, 245, 249));
		std::string tiesText = string::f("TIES: %d", module->cachedTieCount);
		nvgText(vg, box.size.x * 0.5f, 20.0f, tiesText.c_str(), NULL);

		// Line 3: Wires Count
		std::string wiresText = string::f("WIRES: %d", module->cachedWireCount);
		nvgText(vg, box.size.x * 0.5f, 34.0f, wiresText.c_str(), NULL);

		// Line 4: Shortcut Hint
		nvgFontSize(vg, 7.5f);
		nvgFillColor(vg, nvgRGB(56, 189, 248));
		std::string scName = module->getShortcutName();
		std::string hint = string::f("[%s + DRAG]", scName.c_str());
		nvgText(vg, box.size.x * 0.5f, 50.0f, hint.c_str(), NULL);

		nvgRestore(vg);
	}
};

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

struct ZipTieWidget : app::ModuleWidget {
	HarnessOverlayWidget* overlayWidget = nullptr;
	StatusDisplayWidget* displayWidget = nullptr;

	ZipTieWidget(ZipTieModule* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/ZipTie.svg")));

		// Screws
		addChild(createWidget<ScrewSilver>(math::Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(math::Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(math::Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(math::Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Digital Status Display
		displayWidget = new StatusDisplayWidget;
		displayWidget->module = module;
		displayWidget->box.pos = math::Vec(9, 54);
		displayWidget->box.size = math::Vec(72, 68);
		addChild(displayWidget);

		// Section: ARM TOOL Illuminated Bezel Button
		addParam(createLightParamCentered<VCVLightBezel<BlueLight>>(
			math::Vec(45, 168), module, ZipTieModule::PARAM_ARM_TOOL, ZipTieModule::LIGHT_ARMED));

		// Section: SPACING Knob
		addParam(createParamCentered<RoundBlackKnob>(math::Vec(45, 242), module, ZipTieModule::PARAM_WIRE_SPACING));

		// Section: SNIP ALL Button
		addParam(createParamCentered<TL1105>(math::Vec(45, 315), module, ZipTieModule::PARAM_SNIP_ALL));

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
		menu->addChild(createMenuLabel("Zip Ties Wire Management"));

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

		// Style submenu
		menu->addChild(createSubmenuItem("Zip Tie Color Style", "", [m](ui::Menu* sub) {
			sub->addChild(createCheckMenuItem("White Nylon", "",
				[m]() { return m->params[ZipTieModule::PARAM_TIE_STYLE].getValue() == 0.f; },
				[m]() { m->params[ZipTieModule::PARAM_TIE_STYLE].setValue(0.f); }
			));
			sub->addChild(createCheckMenuItem("Dark Carbon", "",
				[m]() { return m->params[ZipTieModule::PARAM_TIE_STYLE].getValue() == 1.f; },
				[m]() { m->params[ZipTieModule::PARAM_TIE_STYLE].setValue(1.f); }
			));
			sub->addChild(createCheckMenuItem("Hi-Viz Yellow", "",
				[m]() { return m->params[ZipTieModule::PARAM_TIE_STYLE].getValue() == 2.f; },
				[m]() { m->params[ZipTieModule::PARAM_TIE_STYLE].setValue(2.f); }
			));
			sub->addChild(createCheckMenuItem("Crimson", "",
				[m]() { return m->params[ZipTieModule::PARAM_TIE_STYLE].getValue() == 3.f; },
				[m]() { m->params[ZipTieModule::PARAM_TIE_STYLE].setValue(3.f); }
			));
		}));
	}
};

Model* modelZipTie = createModel<ZipTieModule, ZipTieWidget>("ZipTie");

} // namespace ZipTies
