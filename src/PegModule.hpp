#pragma once
#include <rack.hpp>

namespace ZipTies {

using namespace rack;

struct PegModule : engine::Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	static constexpr float PEG_X = 15.0f;
	static constexpr float TOP_PEG_Y = 58.0f;
	static constexpr float BOT_PEG_Y = 322.0f;

	// Module IDs of linked PegModules (-1 = not linked)
	int64_t linkedAboveId = -1;
	int64_t linkedBelowId = -1;
	int64_t linkedLeftId = -1;
	int64_t linkedRightId = -1;

	// Tracks when the user explicitly unlinked a vertically aligned neighbor via the button/menu
	int64_t manualUnlinkAboveId = -1;
	int64_t manualUnlinkBelowId = -1;

	PegModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	}

	void process(const ProcessArgs& args) override {
		// Audio thread: zero-cost no-op (all routing & link visuals run on UI thread)
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		linkedAboveId = -1;
		linkedBelowId = -1;
		linkedLeftId = -1;
		linkedRightId = -1;
		manualUnlinkAboveId = -1;
		manualUnlinkBelowId = -1;
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "linkedAboveId", json_integer(linkedAboveId));
		json_object_set_new(rootJ, "linkedBelowId", json_integer(linkedBelowId));
		json_object_set_new(rootJ, "linkedLeftId", json_integer(linkedLeftId));
		json_object_set_new(rootJ, "linkedRightId", json_integer(linkedRightId));
		json_object_set_new(rootJ, "manualUnlinkAboveId", json_integer(manualUnlinkAboveId));
		json_object_set_new(rootJ, "manualUnlinkBelowId", json_integer(manualUnlinkBelowId));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* aboveJ = json_object_get(rootJ, "linkedAboveId");
		if (aboveJ) linkedAboveId = json_integer_value(aboveJ);

		json_t* belowJ = json_object_get(rootJ, "linkedBelowId");
		if (belowJ) linkedBelowId = json_integer_value(belowJ);

		json_t* leftJ = json_object_get(rootJ, "linkedLeftId");
		if (leftJ) linkedLeftId = json_integer_value(leftJ);

		json_t* rightJ = json_object_get(rootJ, "linkedRightId");
		if (rightJ) linkedRightId = json_integer_value(rightJ);

		json_t* manAboveJ = json_object_get(rootJ, "manualUnlinkAboveId");
		if (manAboveJ) manualUnlinkAboveId = json_integer_value(manAboveJ);

		json_t* manBelowJ = json_object_get(rootJ, "manualUnlinkBelowId");
		if (manBelowJ) manualUnlinkBelowId = json_integer_value(manBelowJ);
	}
};

struct PegWidget : app::ModuleWidget {
	widget::Widget* topLinkBtn = nullptr;
	widget::Widget* botLinkBtn = nullptr;

	PegWidget(PegModule* module);
	~PegWidget() override;

	void step() override;
	void appendContextMenu(ui::Menu* menu) override;
};

} // namespace ZipTies
