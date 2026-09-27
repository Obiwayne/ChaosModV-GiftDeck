#include <stdafx.h>

#include "GiftDeckLink.h"

#include "Components/EffectDispatcher.h"
#include "Effects/EffectConfig.h"
#include "Effects/EnabledEffects.h"
#include "Effects/Register/RegisteredEffectsMetadata.h"
#include "Info.h"
#include "Util/File.h"
#include "Util/GameLinkClient.h"
#include "Util/OptionsManager.h"
#include "Util/Text.h"

#include <algorithm>

using nlohmann::json;

static std::unique_ptr<GameLinkClient> ms_Client;

static const char *TimedDescription(EffectTimedType timedType)
{
	switch (timedType)
	{
	case EffectTimedType::NotTimed:
		return "Instant effect";
	case EffectTimedType::Normal:
		return "Timed effect";
	case EffectTimedType::Short:
		return "Timed effect (short)";
	case EffectTimedType::Custom:
		return "Timed effect (custom duration)";
	case EffectTimedType::Permanent:
		return "Permanent effect (set in the Chaos Mod config, can't be triggered)";
	}
	return "";
}

static int CategoryOrder(const std::string &category)
{
	static const std::vector<std::string> order = { "Player", "Vehicle", "Peds", "Screen", "Time",
		                                            "Weather", "Misc",    "Meta", "Scripts" };
	auto it = std::find(order.begin(), order.end(), category);
	return static_cast<int>(it - order.begin());
}

json GiftDeckLink_BuildHello()
{
	struct Entry
	{
		std::string Id, Name, Category, Description;
	};
	std::vector<Entry> entries;

	// Every built-in effect, enabled or not
	for (const auto &[effectId, metadata] : g_RegisteredEffectsMetadata)
	{
		Entry entry;
		entry.Id   = std::string(effectId);
		entry.Name = std::string(metadata.Name);

		auto categoryIt = g_RegisteredEffectsSourceCategory.find(effectId);
		entry.Category =
		    categoryIt != g_RegisteredEffectsSourceCategory.end() ? std::string(categoryIt->second) : "Misc";

		// The few effects whose source folder differs from the category the Chaos Mod config app shows them in
		static const std::unordered_map<std::string_view, const char *> configAppCategory = {
			{ "spawn_bluesultan", "Peds" },
			{ "screen_maximap", "Misc" },
			{ "misc_midas", "Player" },
			{ "misc_onebullet", "Peds" },
		};
		if (auto it = configAppCategory.find(effectId); it != configAppCategory.end())
			entry.Category = it->second;

		auto enabledIt = g_EnabledEffects.find(EffectIdentifier(entry.Id));
		if (enabledIt == g_EnabledEffects.end())
		{
			entry.Description = std::string(metadata.IsTimed ? (metadata.IsShortDuration ? "Timed effect (short)"
			                                                                              : "Timed effect")
			                                                 : "Instant effect")
			                  + ". Disabled in the Chaos Mod config";
		}
		else
		{
			const auto &effectData = enabledIt->second;
			entry.Description      = TimedDescription(effectData.TimedType);
			if (effectData.IsGiftDeckOnly())
				entry.Description += ". Disabled for random picks in the Chaos Mod config (GiftDeck can still start it)";
			if (effectData.HasCustomName())
				entry.Description += ". Shown in game as \"" + effectData.CustomName + "\"";
		}

		entries.push_back(std::move(entry));
	}

	// Lua script effects (chaosmod/scripts, workshop) are only known once LuaScripts has loaded them
	for (const auto &[effectId, effectData] : g_EnabledEffects)
	{
		if (g_RegisteredEffectsMetadata.contains(effectId.Id()) || effectData.IsTemporary())
			continue;

		Entry entry;
		entry.Id          = effectId.Id();
		entry.Name        = effectData.HasCustomName() ? effectData.CustomName : effectData.Name;
		entry.Category    = effectData.IsMeta() ? "Meta" : "Scripts";
		entry.Description = std::string(TimedDescription(effectData.TimedType)) + ". Lua script effect";
		entries.push_back(std::move(entry));
	}

	std::sort(entries.begin(), entries.end(),
	          [](const Entry &a, const Entry &b)
	          {
		          auto ca = CategoryOrder(a.Category), cb = CategoryOrder(b.Category);
		          if (ca != cb)
			          return ca < cb;
		          if (a.Category != b.Category)
			          return a.Category < b.Category;
		          if (a.Name != b.Name)
			          return a.Name < b.Name;
		          return a.Id < b.Id;
	          });

	json commands = json::array();
	for (const auto &entry : entries)
	{
		commands.push_back({ { "id", entry.Id },
		                     { "name", entry.Name },
		                     { "category", entry.Category },
		                     { "description", entry.Description },
		                     { "args", json::array() } });
	}

	return json { { "type", "hello" },
		          { "game", "gta5" },
		          { "mod", "chaosmod" },
		          { "name", "Chaos Mod V" },
		          { "version", MOD_VERSION "-giftdeck." GIFTDECK_BUILD_VERSION },
		          { "commands", commands } };
}

static std::string DumpJson(const json &message, int indent = -1)
{
	return message.dump(indent, ' ', false, json::error_handler_t::replace);
}

// "(nickname)" shown after the effect name; strips GTA text formatting (~r~ etc.) and keeps it short
static std::string MakeUserSuffix(const std::string &user)
{
	std::string clean;
	int codepoints = 0;
	for (size_t i = 0; i < user.size(); i++)
	{
		unsigned char c = user[i];
		if (c == '~' || c < 0x20)
			continue;

		bool isLead = (c & 0xC0) != 0x80;
		if (isLead && ++codepoints > 24)
			break;
		clean += static_cast<char>(c);
	}

	clean = StringTrim(clean);
	return clean.empty() ? "" : "(" + clean + ")";
}

// Checks whether a trigger's effect can be started. Returns the effect's identifier, or nullptr with the reason in
// outMessage (on success outMessage is the quoted effect name).
static const EffectIdentifier *CheckTrigger(const std::string &command, std::string &outMessage)
{
	const EffectIdentifier effectId(command);

	auto enabledIt = g_EnabledEffects.find(effectId);
	if (enabledIt == g_EnabledEffects.end())
	{
		if (g_RegisteredEffectsMetadata.contains(command))
			outMessage = "Effect \"" + command + "\" is disabled in the Chaos Mod config";
		else
			outMessage = "Unknown effect id \"" + command + "\"";
		return nullptr;
	}

	const auto &effectData = enabledIt->second;
	const auto &name       = effectData.HasCustomName() ? effectData.CustomName : effectData.Name;

	if (effectData.TimedType == EffectTimedType::Permanent)
	{
		outMessage = "\"" + name + "\" is set to permanent in the Chaos Mod config and can't be triggered";
		return nullptr;
	}

	if (effectData.IsTemporary())
	{
		outMessage = "\"" + name + "\" is a temporary script effect";
		return nullptr;
	}

	if (IsEffectFilteredOut(effectId))
	{
		auto reason = GetFilterReason(effectId);
		outMessage  = "\"" + name + "\" can't run right now" + (reason.empty() ? "" : ": " + reason);
		return nullptr;
	}

	outMessage = "\"" + name + "\"";
	return &enabledIt->first;
}

static void LoadEffectsConfig()
{
	g_EnabledEffects.clear();
	EffectConfig::ReadConfig(
	    { "chaosmod/configs/effects.json", "chaosmod/configs/effects.ini", "chaosmod/effects.ini" }, g_EnabledEffects,
	    g_OptionsManager.GetConfigValue({ "GiftDeckRunDisabledEffects" }, true));
}

GiftDeckLink::GiftDeckLink() : Component()
{
	m_Enabled          = g_OptionsManager.GetConfigValue({ "EnableGiftDeckLink" }, true);
	m_ShowUserInEffect = g_OptionsManager.GetConfigValue({ "GiftDeckShowUserInEffectName" }, true);

	auto hello         = GiftDeckLink_BuildHello();

	if (DoesFeatureFlagExist("giftdeckdumphello"))
	{
		std::ofstream file("chaosmod\\giftdeck_hello.json", std::ios::binary);
		file << DumpJson(hello, 2);
		LOG("Wrote GameLink hello (" << hello["commands"].size() << " effects) to chaosmod\\giftdeck_hello.json");
	}

	if (!m_Enabled)
	{
		LOG("GiftDeck link disabled in config (EnableGiftDeckLink)");
		GiftDeckLink_Shutdown();
		return;
	}

	if (!ms_Client)
	{
		GameLinkClient::Options options;
		options.Port =
		    static_cast<unsigned short>(g_OptionsManager.GetConfigValue({ "GiftDeckLinkPort" }, 21216));
		ms_Client = std::make_unique<GameLinkClient>(options, [](const std::string &text) { LOG(text); });
		LOG("GiftDeck link: connecting to ws://127.0.0.1:" << options.Port << "/ ("
		                                                   << hello["commands"].size() << " effects)");
	}

	ms_Client->SetHello(DumpJson(hello));
	ms_Client->Start();
}

void GiftDeckLink::OnRun()
{
	if (!m_Enabled || !ms_Client)
		return;

	ms_Client->Heartbeat();
	ms_Client->SetStatus("In game");

	GameLinkClient::Trigger trigger;
	while (ms_Client->PopTrigger(trigger))
	{
		std::string message;
		auto effectId = CheckTrigger(trigger.Command, message);
		if (!effectId)
		{
			ms_Client->SendResult(trigger.Id, false, message);
			continue;
		}

		if (!ComponentExists<EffectDispatcher>())
		{
			ms_Client->SendResult(trigger.Id, false, "Chaos Mod effect dispatcher is not running");
			continue;
		}

		LOG("GiftDeck trigger: " << message << " (" << trigger.Command << ") from " << trigger.User << " / "
		                         << trigger.Gift << " x" << trigger.Count);

		GetComponent<EffectDispatcher>()->DispatchEffect(*effectId, EffectDispatcher::DispatchEffectFlag_None,
		                                                 m_ShowUserInEffect ? MakeUserSuffix(trigger.User) : "");

		ms_Client->SendResult(trigger.Id, true, "Started " + message);
	}
}

void GiftDeckLink_Shutdown()
{
	if (ms_Client)
	{
		ms_Client->Stop();
		ms_Client.reset();
	}
}

// Offline dump used by the test harness (tests/giftdeck): loads config.json + effects.json from the current directory
// (chaosmod/configs/) and writes the hello message that would be sent to GiftDeck.
// The mod is loaded with CHAOSMOD_GIFTDECK_DUMP set so DllMain skips all game hooks.
extern "C" __declspec(dllexport) int GiftDeckDumpHello(const char *outPath)
{
	g_OptionsManager.Reset();
	LoadEffectsConfig();

	auto hello = GiftDeckLink_BuildHello();

	std::ofstream file(outPath, std::ios::binary);
	if (!file)
		return -1;
	file << DumpJson(hello, 2);

	return static_cast<int>(hello["commands"].size());
}

// Offline trigger check used by the test harness: runs the same checks as a GiftDeck trigger (without starting the
// effect) against the config loaded by GiftDeckDumpHello. Returns 1 = would start, 0 = refused; message in outMessage.
extern "C" __declspec(dllexport) int GiftDeckCheckTrigger(const char *command, char *outMessage, int outSize)
{
	std::string message;
	bool ok = CheckTrigger(command, message) != nullptr;
	if (outMessage && outSize > 0)
	{
		auto len = std::min<size_t>(message.size(), static_cast<size_t>(outSize - 1));
		memcpy(outMessage, message.data(), len);
		outMessage[len] = 0;
	}
	return ok ? 1 : 0;
}
