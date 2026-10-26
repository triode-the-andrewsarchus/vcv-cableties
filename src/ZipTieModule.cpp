#include "ZipTieModule.hpp"
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>

namespace ZipTies {

ZipTieModule::ZipTieModule() {
	config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

	configButton(PARAM_ARM_TOOL, "Arm Tie Tool (Draw lines to tie without holding shortcut)");
	configParam(PARAM_WIRE_SPACING, 2.0f, 6.0f, 3.5f, "Bundle Wire Spacing", " px");
	configSwitch(PARAM_TIE_STYLE, 0.f, 3.f, 0.f, "Zip Tie Color Style", {
		"White Nylon",
		"Dark Carbon",
		"Hi-Viz Yellow",
		"Crimson"
	});
	configButton(PARAM_SNIP_ALL, "Snip / Remove All Zip Ties");
}

ZipTieModule::~ZipTieModule() {
	std::lock_guard<std::recursive_mutex> lock(stateMutex);
	if (APP && APP->scene && APP->scene->rack) {
		for (auto& pair : cableTieSequences) {
			app::CableWidget* cw = APP->scene->rack->getCable(pair.first);
			if (cw) {
				cw->visible = true;
			}
		}
	}
}

void ZipTieModule::onReset(const ResetEvent& e) {
	Module::onReset(e);
	clearAllZipTies();
	toolArmed = false;
	learningKey = false;
	pendingMods = 0;
	shortcutKey = GLFW_KEY_Z;
	shortcutMods = 0;
}

void ZipTieModule::process(const ProcessArgs& args) {
	// Check Snip All button
	if (snipTrigger.process(params[PARAM_SNIP_ALL].getValue() > 0.5f)) {
		clearAllZipTies();
	}

	// Check Arm Tool button
	if (armTrigger.process(params[PARAM_ARM_TOOL].getValue() > 0.5f)) {
		toolArmed = !toolArmed;
	}

	// Update light
	lights[LIGHT_ARMED].setBrightness(toolArmed ? 1.0f : 0.0f);

	// Update cached counts
	cachedTieCount = (int)zipTies.size();
	cachedWireCount = (int)cableTieSequences.size();
}

int ZipTieModule::addZipTie(math::Vec pos, const std::vector<int64_t>& cableIds, const std::map<int64_t, int>& insertPositions) {
	std::lock_guard<std::recursive_mutex> lock(stateMutex);

	ZipTie tie;
	tie.id = nextTieId++;
	tie.pos = pos;
	tie.cableIds = cableIds;
	tie.wireSpacing = params[PARAM_WIRE_SPACING].getValue();

	// Style color
	int style = (int)params[PARAM_TIE_STYLE].getValue();
	if (style == 1) {
		tie.color = nvgRGB(35, 38, 42); // Dark carbon
	}
	else if (style == 2) {
		tie.color = nvgRGB(245, 220, 40); // Hi-viz yellow
	}
	else if (style == 3) {
		tie.color = nvgRGB(230, 60, 50); // Crimson
	}
	else {
		tie.color = nvgRGB(235, 238, 242); // White nylon
	}

	zipTies.push_back(tie);

	// Insert into each cable's sequence at the appropriate topological index
	for (int64_t cid : cableIds) {
		auto& seq = cableTieSequences[cid];
		int insertIdx = (int)seq.size();
		auto it = insertPositions.find(cid);
		if (it != insertPositions.end()) {
			insertIdx = std::max(0, std::min((int)seq.size(), it->second));
		}
		seq.insert(seq.begin() + insertIdx, tie.id);

		// Hide default cable drawing so our harness draws it
		if (APP && APP->scene && APP->scene->rack) {
			app::CableWidget* cw = APP->scene->rack->getCable(cid);
			if (cw) {
				cw->visible = false;
			}
		}
	}

	cachedTieCount = (int)zipTies.size();
	cachedWireCount = (int)cableTieSequences.size();
	return tie.id;
}

void ZipTieModule::removeZipTie(int tieId) {
	std::lock_guard<std::recursive_mutex> lock(stateMutex);

	// Remove from zipTies
	zipTies.erase(std::remove_if(zipTies.begin(), zipTies.end(), [tieId](const ZipTie& z) {
		return z.id == tieId;
	}), zipTies.end());

	// Remove from all cable sequences
	std::vector<int64_t> emptyCables;
	for (auto& pair : cableTieSequences) {
		auto& seq = pair.second;
		seq.erase(std::remove(seq.begin(), seq.end(), tieId), seq.end());
		if (seq.empty()) {
			emptyCables.push_back(pair.first);
			if (APP && APP->scene && APP->scene->rack) {
				app::CableWidget* cw = APP->scene->rack->getCable(pair.first);
				if (cw) {
					cw->visible = true; // Restore normal Rack cable
				}
			}
		}
	}

	for (int64_t cid : emptyCables) {
		cableTieSequences.erase(cid);
	}

	cachedTieCount = (int)zipTies.size();
	cachedWireCount = (int)cableTieSequences.size();
}

void ZipTieModule::moveZipTie(int tieId, math::Vec newPos) {
	std::lock_guard<std::recursive_mutex> lock(stateMutex);
	for (ZipTie& z : zipTies) {
		if (z.id == tieId) {
			z.pos = newPos;
			break;
		}
	}
}

void ZipTieModule::clearAllZipTies() {
	std::lock_guard<std::recursive_mutex> lock(stateMutex);
	if (APP && APP->scene && APP->scene->rack) {
		for (auto& pair : cableTieSequences) {
			app::CableWidget* cw = APP->scene->rack->getCable(pair.first);
			if (cw) {
				cw->visible = true;
			}
		}
	}
	zipTies.clear();
	cableTieSequences.clear();
	cachedTieCount = 0;
	cachedWireCount = 0;
}

void ZipTieModule::cleanDeadCables() {
	if (!APP || !APP->scene || !APP->scene->rack || !APP->engine) return;
	std::lock_guard<std::recursive_mutex> lock(stateMutex);

	// Keep wire spacing & style synced with module params
	float currentSpacing = params[PARAM_WIRE_SPACING].getValue();
	int style = (int)params[PARAM_TIE_STYLE].getValue();
	NVGcolor styleColor = nvgRGB(235, 238, 242);
	if (style == 1) styleColor = nvgRGB(35, 38, 42);
	else if (style == 2) styleColor = nvgRGB(245, 220, 40);
	else if (style == 3) styleColor = nvgRGB(230, 60, 50);

	for (ZipTie& z : zipTies) {
		z.wireSpacing = currentSpacing;
		z.color = styleColor;
	}

	std::vector<int64_t> deadCables;
	for (auto& pair : cableTieSequences) {
		int64_t cid = pair.first;
		app::CableWidget* cw = APP->scene->rack->getCable(cid);
		engine::Cable* ec = APP->engine->getCable(cid);
		if (!cw && !ec) {
			deadCables.push_back(cid);
		}
	}

	if (deadCables.empty()) return;

	for (int64_t cid : deadCables) {
		cableTieSequences.erase(cid);
		for (ZipTie& z : zipTies) {
			z.removeCable(cid);
		}
	}

	// Remove zip ties that have no cables left
	zipTies.erase(std::remove_if(zipTies.begin(), zipTies.end(), [](const ZipTie& z) {
		return z.cableIds.empty();
	}), zipTies.end());

	cachedTieCount = (int)zipTies.size();
	cachedWireCount = (int)cableTieSequences.size();
}

bool ZipTieModule::isCableTied(int64_t cableId) const {
	std::lock_guard<std::recursive_mutex> lock(stateMutex);
	auto it = cableTieSequences.find(cableId);
	return it != cableTieSequences.end() && !it->second.empty();
}

const ZipTie* ZipTieModule::getZipTie(int tieId) const {
	for (const ZipTie& z : zipTies) {
		if (z.id == tieId) return &z;
	}
	return nullptr;
}

json_t* ZipTieModule::dataToJson() {
	std::lock_guard<std::recursive_mutex> lock(stateMutex);
	json_t* rootJ = json_object();

	json_object_set_new(rootJ, "toolArmed", json_boolean(toolArmed));
	json_object_set_new(rootJ, "nextTieId", json_integer(nextTieId));
	json_object_set_new(rootJ, "shortcutKey", json_integer(shortcutKey));
	json_object_set_new(rootJ, "shortcutMods", json_integer(shortcutMods));

	// Save zip ties
	json_t* tiesJ = json_array();
	for (const ZipTie& z : zipTies) {
		json_t* tieJ = json_object();
		json_object_set_new(tieJ, "id", json_integer(z.id));
		json_object_set_new(tieJ, "x", json_real(z.pos.x));
		json_object_set_new(tieJ, "y", json_real(z.pos.y));
		json_object_set_new(tieJ, "spacing", json_real(z.wireSpacing));

		json_t* cidsJ = json_array();
		for (int64_t cid : z.cableIds) {
			json_array_append_new(cidsJ, json_integer(cid));
		}
		json_object_set_new(tieJ, "cableIds", cidsJ);
		json_array_append_new(tiesJ, tieJ);
	}
	json_object_set_new(rootJ, "zipTies", tiesJ);

	// Save cable sequences
	json_t* seqsJ = json_object();
	for (const auto& pair : cableTieSequences) {
		json_t* seqJ = json_array();
		for (int tid : pair.second) {
			json_array_append_new(seqJ, json_integer(tid));
		}
		std::string key = std::to_string(pair.first);
		json_object_set_new(seqsJ, key.c_str(), seqJ);
	}
	json_object_set_new(rootJ, "cableSequences", seqsJ);

	return rootJ;
}

void ZipTieModule::dataFromJson(json_t* rootJ) {
	std::lock_guard<std::recursive_mutex> lock(stateMutex);
	clearAllZipTies();

	json_t* armedJ = json_object_get(rootJ, "toolArmed");
	if (armedJ) toolArmed = json_is_true(armedJ);

	json_t* nextIdJ = json_object_get(rootJ, "nextTieId");
	if (nextIdJ) nextTieId = (int)json_integer_value(nextIdJ);

	json_t* shortcutKeyJ = json_object_get(rootJ, "shortcutKey");
	if (shortcutKeyJ) shortcutKey = (int)json_integer_value(shortcutKeyJ);

	json_t* shortcutModsJ = json_object_get(rootJ, "shortcutMods");
	if (shortcutModsJ) shortcutMods = (int)json_integer_value(shortcutModsJ);

	// Restore zip ties
	json_t* tiesJ = json_object_get(rootJ, "zipTies");
	if (tiesJ && json_is_array(tiesJ)) {
		size_t idx;
		json_t* tieJ;
		json_array_foreach(tiesJ, idx, tieJ) {
			ZipTie z;
			json_t* idJ = json_object_get(tieJ, "id");
			if (idJ) z.id = (int)json_integer_value(idJ);

			json_t* xJ = json_object_get(tieJ, "x");
			json_t* yJ = json_object_get(tieJ, "y");
			if (xJ && yJ) z.pos = math::Vec(json_real_value(xJ), json_real_value(yJ));

			json_t* spJ = json_object_get(tieJ, "spacing");
			if (spJ) z.wireSpacing = json_real_value(spJ);

			json_t* cidsJ = json_object_get(tieJ, "cableIds");
			if (cidsJ && json_is_array(cidsJ)) {
				size_t cidx;
				json_t* cidJ;
				json_array_foreach(cidsJ, cidx, cidJ) {
					z.cableIds.push_back(json_integer_value(cidJ));
				}
			}
			zipTies.push_back(z);
		}
	}

	// Restore sequences
	json_t* seqsJ = json_object_get(rootJ, "cableSequences");
	if (seqsJ && json_is_object(seqsJ)) {
		const char* key;
		json_t* seqJ;
		json_object_foreach(seqsJ, key, seqJ) {
			int64_t cid = std::stoll(key);
			std::vector<int> seq;
			if (json_is_array(seqJ)) {
				size_t sidx;
				json_t* tidJ;
				json_array_foreach(seqJ, sidx, tidJ) {
					seq.push_back((int)json_integer_value(tidJ));
				}
			}
			cableTieSequences[cid] = seq;
		}
	}

	cachedTieCount = (int)zipTies.size();
	cachedWireCount = (int)cableTieSequences.size();
}

} // namespace ZipTies
