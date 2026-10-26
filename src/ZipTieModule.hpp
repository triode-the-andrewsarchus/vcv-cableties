#pragma once
#include <rack.hpp>
#include "ZipTieModel.hpp"
#include <map>
#include <vector>
#include <mutex>

namespace ZipTies {

using namespace rack;

inline bool isModifierKey(int key) {
	return key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT ||
	       key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL ||
	       key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT ||
	       key == GLFW_KEY_LEFT_SUPER || key == GLFW_KEY_RIGHT_SUPER;
}

inline int modifierBitForKey(int key) {
	if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT)
		return GLFW_MOD_SHIFT;
	if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL)
		return GLFW_MOD_CONTROL;
	if (key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT)
		return GLFW_MOD_ALT;
	if (key == GLFW_KEY_LEFT_SUPER || key == GLFW_KEY_RIGHT_SUPER)
		return GLFW_MOD_SUPER;
	return 0;
}

inline std::string formatKeyCombo(int key, int mods) {
	std::string s;
	if (mods & RACK_MOD_CTRL) {
		s += RACK_MOD_CTRL_NAME "+";
	}
	if (mods & GLFW_MOD_ALT) {
		s += RACK_MOD_ALT_NAME "+";
	}
	if (mods & GLFW_MOD_SHIFT) {
		s += RACK_MOD_SHIFT_NAME "+";
	}
#if !defined ARCH_MAC
	if (mods & GLFW_MOD_SUPER) {
		s += "Super+";
	}
#endif

	if (key > 0 && !isModifierKey(key)) {
		std::string kName = widget::getKeyName(key);
		if (kName.empty()) {
			if (key == GLFW_KEY_SPACE) kName = "Space";
			else if (key == GLFW_KEY_TAB) kName = "Tab";
			else if (key == GLFW_KEY_ENTER) kName = "Enter";
			else if (key == GLFW_KEY_BACKSPACE) kName = "Backspace";
			else kName = string::f("Key %d", key);
		}
		s += kName;
	}
	else if (!s.empty() && s.back() == '+') {
		s.pop_back();
	}

	if (s.empty()) {
		return "None";
	}
	return s;
}

struct ZipTieModule : engine::Module {
	enum ParamIds {
		PARAM_ARM_TOOL,
		PARAM_WIRE_SPACING,
		PARAM_TIE_STYLE,
		PARAM_SNIP_ALL,
		NUM_PARAMS
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		NUM_OUTPUTS
	};
	enum LightIds {
		LIGHT_ARMED,
		NUM_LIGHTS
	};

	// State
	mutable std::recursive_mutex stateMutex;
	std::vector<ZipTie> zipTies;
	std::map<int64_t, std::vector<int>> cableTieSequences;
	int nextTieId = 1;

	// User-configurable shortcut key & modifiers (chosen in right-click menu)
	int shortcutKey = GLFW_KEY_Z;
	int shortcutMods = 0;
	bool learningKey = false;
	int pendingMods = 0;

	// UI triggers
	dsp::BooleanTrigger snipTrigger;
	dsp::BooleanTrigger armTrigger;
	bool toolArmed = false;

	// Cached statistics
	int cachedTieCount = 0;
	int cachedWireCount = 0;

	ZipTieModule();
	~ZipTieModule() override;

	void process(const ProcessArgs& args) override;
	void onReset(const ResetEvent& e) override;

	std::string getShortcutName() const {
		return formatKeyCombo(shortcutKey, shortcutMods);
	}

	void setShortcut(int key, int mods) {
		shortcutKey = key;
		shortcutMods = mods & RACK_MOD_MASK;
		learningKey = false;
		pendingMods = 0;
	}

	// Zip tie management
	int addZipTie(math::Vec pos, const std::vector<int64_t>& cableIds, const std::map<int64_t, int>& insertPositions);
	void removeZipTie(int tieId);
	void moveZipTie(int tieId, math::Vec newPos);
	void clearAllZipTies();
	void cleanDeadCables();
	bool isCableTied(int64_t cableId) const;
	const ZipTie* getZipTie(int tieId) const;

	// JSON serialization
	json_t* dataToJson() override;
	void dataFromJson(json_t* rootJ) override;
};

} // namespace ZipTies
