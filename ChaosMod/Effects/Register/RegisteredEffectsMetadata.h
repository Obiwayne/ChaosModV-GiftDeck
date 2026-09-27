#pragma once

#include "Effects/Condition/EffectCondition.h"
#include "Effects/EffectCategory.h"
#include "Effects/EffectExecutionType.h"
#include "Effects/EffectGroups.h"

#include <string_view>
#include <unordered_map>
#include <vector>

struct RegisteredEffectMetadata
{
	std::string_view Name;
	std::string_view Id;
	bool IsTimed             = false;
	bool IsShortDuration     = false;
	bool HideRealNameOnStart = false;
#ifdef CHAOSDEBUG
	int DebugShortcutKeycode = 0;
#endif
	std::vector<std::string_view> IncompatibleWith;
	EffectCategory EffectCategory     = EffectCategory::None;
	EffectGroupType EffectGroupType   = EffectGroupType::None;
	EffectExecutionType ExecutionType = EffectExecutionType::Default;
	EffectConditionType ConditionType = EffectConditionType::None;
};

inline std::unordered_map<std::string_view, RegisteredEffectMetadata> g_RegisteredEffectsMetadata;

// GiftDeck build: category of each built-in effect (Player, Peds, Vehicle, Misc, Time, Weather, Screen, Meta), derived
// from the Effects/db/<Folder>/ source folder the effect is registered in
inline std::unordered_map<std::string_view, std::string_view> g_RegisteredEffectsSourceCategory;

inline std::string_view GetEffectCategoryFromSourcePath(std::string_view path)
{
	std::string_view folder;
	for (auto marker : { "\\db\\", "/db/" })
	{
		auto pos = path.rfind(marker);
		if (pos == path.npos)
			continue;

		auto rest = path.substr(pos + std::string_view(marker).size());
		auto end  = rest.find_first_of("\\/");
		if (end == rest.npos)
			continue;

		folder = rest.substr(0, end);
		break;
	}

	if (folder == "Vehs")
		return "Vehicle";
	if (folder.empty())
		return "Misc";
	return folder;
}
