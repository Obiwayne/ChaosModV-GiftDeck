#include <stdafx.h>

#include "Info.h"
#include "Main.h"

#include "Memory/Memory.h"

#include "Util/CrashHandler.h"

// GiftDeck build: set by the offline effect-list dump harness (tests/giftdeck) which loads the .asi outside of the game
// with a stub ScriptHookV.dll. Skips every game hook / script registration.
static bool IsOfflineDumpHost()
{
	return GetEnvironmentVariableA("CHAOSMOD_GIFTDECK_DUMP", nullptr, 0) > 0;
}

BOOL APIENTRY DllMain(HMODULE instance, DWORD reason, LPVOID reserved)
{
	if (IsOfflineDumpHost())
		return TRUE;

	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
	{
		SetUnhandledExceptionFilter(CrashHandler);

		RAW_LOG("Chaos Mod v" MOD_VERSION "\n\n");

		Memory::Init();

		scriptRegister(instance, Main::OnRun);

		keyboardHandlerRegister(Main::OnKeyboardInput);

		break;
	}
	case DLL_PROCESS_DETACH:
		Memory::Uninit();

		scriptUnregister(instance);

		keyboardHandlerUnregister(Main::OnKeyboardInput);

		if (GetConsoleWindow())
		{
			g_ConsoleOut.close();

			FreeConsole();
		}

		break;
	}

	return TRUE;
}