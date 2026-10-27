#pragma once
#include <rack.hpp>
#include "CableTieModel.hpp"
#include <atomic>
#include <map>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace CableTies {

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

inline void setPlugRotation(app::PlugWidget* plug, float angle) {
	if (!plug || plug->children.empty()) return;
	widget::FramebufferWidget* fb = dynamic_cast<widget::FramebufferWidget*>(plug->children.front());
	if (!fb || fb->children.empty()) return;
	widget::TransformWidget* tw = dynamic_cast<widget::TransformWidget*>(fb->children.front());
	if (!tw) return;

	math::Vec center = tw->box.size.div(2.f);
	float targetAngle = angle - 0.5f * float(M_PI);

	float expected[6];
	nvgTransformIdentity(expected);
	float t[6];
	nvgTransformTranslate(t, center.x, center.y);
	nvgTransformPremultiply(expected, t);
	nvgTransformRotate(t, targetAngle);
	nvgTransformPremultiply(expected, t);
	nvgTransformTranslate(t, -center.x, -center.y);
	nvgTransformPremultiply(expected, t);

	bool changed = false;
	for (int i = 0; i < 6; ++i) {
		if (std::fabs(tw->transform[i] - expected[i]) > 1e-4f) {
			changed = true;
			break;
		}
	}
	if (changed) {
		for (int i = 0; i < 6; ++i) {
			tw->transform[i] = expected[i];
		}
		fb->setDirty();
	}
}

inline math::Vec getSlumpPos(math::Vec outPos, math::Vec inPos) {
	float dist = outPos.minus(inPos).norm();
	math::Vec slump = outPos.plus(inPos).div(2.f);
	slump.y += (1.0f - settings::cableTension) * (150.0f + 1.0f * dist);
	return slump;
}

inline void restoreCablePlugs(app::CableWidget* cw) {
	if (!cw || !cw->isComplete()) return;
	math::Vec outPos = cw->getOutputPos();
	math::Vec inPos = cw->getInputPos();
	math::Vec slump = getSlumpPos(outPos, inPos);
	setPlugRotation(cw->outputPlug, slump.minus(outPos).arg());
	setPlugRotation(cw->inputPlug, slump.minus(inPos).arg());
}

struct CableTieModule : engine::Module {
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
	std::vector<CableTie> cableTies;
	std::map<int64_t, std::vector<int>> cableTieSequences;
	int nextTieId = 1;

	// User-configurable shortcut key & modifiers (chosen in right-click menu)
	int shortcutKey = GLFW_KEY_C;
	int shortcutMods = 0;
	bool learningKey = false;
	int pendingMods = 0;

	// UI triggers & lock-free audio->UI handoff
	dsp::BooleanTrigger snipTrigger;
	dsp::BooleanTrigger armTrigger;
	bool toolArmed = false;
	std::atomic<bool> clearRequested{false};

	// Cached statistics (UI thread)
	int cachedTieCount = 0;
	int cachedWireCount = 0;

	CableTieModule();
	~CableTieModule() override;

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

	// Cable tie management
	int addCableTie(math::Vec pos, const std::vector<int64_t>& cableIds, const std::map<int64_t, int>& insertPositions);
	void removeCableTie(int tieId);
	void moveCableTie(int tieId, math::Vec newPos);
	void clearAllCableTies();
	void cleanDeadCables(const std::unordered_map<int64_t, app::CableWidget*>& activeCables);
	bool isCableTied(int64_t cableId) const;
	const CableTie* getCableTie(int tieId) const;

	// JSON serialization
	json_t* dataToJson() override;
	void dataFromJson(json_t* rootJ) override;
};

} // namespace CableTies
