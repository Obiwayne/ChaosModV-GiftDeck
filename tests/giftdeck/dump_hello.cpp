// Offline dump of the GameLink hello message (the full effect list) from a built ChaosMod.asi, without the game.
//
// usage: dump_hello.exe <ChaosMod.asi> <work dir containing chaosmod\configs\effects.json (optional)> <out.json>
//
// Sets CHAOSMOD_GIFTDECK_DUMP so the mod's DllMain skips every game hook, loads the .asi (its static initializers
// register all built-in effects exactly as in game) and calls the exported GiftDeckDumpHello().

#include <windows.h>

#include <cstdio>
#include <filesystem>

int wmain(int argc, wchar_t **argv)
{
	if (argc < 4)
	{
		std::fwprintf(stderr, L"usage: dump_hello.exe <ChaosMod.asi> <work dir> <out.json>\n");
		return 2;
	}

	auto asiPath = std::filesystem::absolute(argv[1]);
	auto outPath = std::filesystem::absolute(argv[3]);

	SetEnvironmentVariableW(L"CHAOSMOD_GIFTDECK_DUMP", L"1");
	std::filesystem::create_directories(std::filesystem::path(argv[2]) / "chaosmod");
	SetCurrentDirectoryW(argv[2]); // the mod reads chaosmod\configs\effects.json and writes chaosmod\chaoslog.txt here

	HMODULE mod = LoadLibraryW(asiPath.c_str());
	if (!mod)
	{
		std::fwprintf(stderr, L"LoadLibrary failed: %lu\n", GetLastError());
		return 1;
	}

	using DumpFn = int (*)(const char *);
	auto dump    = reinterpret_cast<DumpFn>(GetProcAddress(mod, "GiftDeckDumpHello"));
	if (!dump)
	{
		std::fwprintf(stderr, L"GiftDeckDumpHello export not found\n");
		return 1;
	}

	int count = dump(outPath.string().c_str());
	std::printf("effects in hello: %d\n", count);
	std::fflush(stdout);

	// Skip DLL teardown (game-side globals were never set up)
	TerminateProcess(GetCurrentProcess(), count > 0 ? 0 : 1);
	return 0;
}
