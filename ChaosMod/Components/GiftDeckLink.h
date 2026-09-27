#pragma once

// GiftDeck build of Chaos Mod V: GameLink client component.
//
// Connects to GiftDeck (ws://127.0.0.1:21216/), announces every registered effect in a "hello" message and starts
// effects immediately when GiftDeck sends a "trigger" (same path as the effect shortcuts / debug menu: no voting, no
// timer). The network side runs on background threads (Util/GameLinkClient); this component only drains the trigger
// queue on the game thread.

#include "Components/Component.h"

#include <json.hpp>

#define GIFTDECK_BUILD_VERSION "1"

class GiftDeckLink : public Component
{
	bool m_Enabled          = true;
	bool m_ShowUserInEffect = true;

  public:
	GiftDeckLink();

	virtual void OnRun() override;
};

// Builds the GameLink hello message from the registered effects and the enabled state in g_EnabledEffects.
nlohmann::json GiftDeckLink_BuildHello();

// Stops the background connection (mod unload). Safe to call when it never started.
void GiftDeckLink_Shutdown();
