// Test harness for ChaosMod/Util/GameLinkClient: a fake "game thread" that behaves like the GiftDeckLink component.
// Run by test_gamelink.py, which plays the GiftDeck side (WebSocket server) and checks the framing.
//
// usage: gamelink_test.exe <port> <seconds to run>
//
// Fake effects: "player_suicide" (enabled), "misc_disabled" (registered but disabled), everything else unknown.
// Special command "test_pause_game" answers ok and then stops calling Heartbeat() for 4 s (simulates a loading
// screen / paused mod) so the "not in game" path can be tested.

#include <stdafx.h>

#include "Util/GameLinkClient.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>

static std::mutex ms_PrintMutex;

static void Print(const std::string &text)
{
	std::lock_guard lock(ms_PrintMutex);
	std::cout << "[client] " << text << std::endl;
}

int main(int argc, char **argv)
{
	unsigned short port = argc > 1 ? static_cast<unsigned short>(std::atoi(argv[1])) : 21216;
	int runSeconds      = argc > 2 ? std::atoi(argv[2]) : 30;

	GameLinkClient::Options options;
	options.Port             = port;
	options.ReconnectDelayMs = 1000;
	options.PingIntervalMs   = 1500;
	options.DeadTimeoutMs    = 6000;

	GameLinkClient client(options, Print);

	nlohmann::json hello = { { "type", "hello" },
		                     { "game", "gta5" },
		                     { "mod", "chaosmod" },
		                     { "name", "Chaos Mod V" },
		                     { "version", "test" },
		                     { "commands",
		                       { { { "id", "player_suicide" },
		                           { "name", "Suicide" },
		                           { "category", "Player" },
		                           { "description", "Instant effect" },
		                           { "args", nlohmann::json::array() } },
		                         { { "id", "misc_disabled" },
		                           { "name", "Disabled Thing \xC3\xA9" },
		                           { "category", "Misc" },
		                           { "description", "Instant effect. Disabled in the Chaos Mod config" },
		                           { "args", nlohmann::json::array() } } } } };
	client.SetHello(hello.dump());
	client.Start();

	const auto start       = GameLinkClient::Now();
	std::uint64_t pausedUntil = 0;
	double maxFrameUs      = 0;
	std::uint64_t frames   = 0;

	while (GameLinkClient::Now() - start < static_cast<std::uint64_t>(runSeconds) * 1000)
	{
		// Time only the client calls the game thread makes (not this harness's console output)
		double frameUs = 0;
		auto timed     = [&](auto &&fn)
		{
			auto t0 = std::chrono::steady_clock::now();
			auto r  = fn();
			frameUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
			return r;
		};

		if (GameLinkClient::Now() >= pausedUntil)
		{
			timed([&] { client.Heartbeat(); client.SetStatus("In game"); return 0; });

			GameLinkClient::Trigger trigger;
			while (timed([&] { return client.PopTrigger(trigger); }))
			{
				Print("trigger " + trigger.Command + " id=" + trigger.Id + " user=" + trigger.User
				      + " gift=" + trigger.Gift + " count=" + std::to_string(trigger.Count)
				      + " args=" + std::to_string(trigger.Args.dump().size()) + "B");

				if (trigger.Command == "player_suicide")
					timed([&] { client.SendResult(trigger.Id, true, "Started \"Suicide\""); return 0; });
				else if (trigger.Command == "misc_disabled")
					timed([&] { client.SendResult(trigger.Id, false,
					                  "Effect \"misc_disabled\" is disabled in the Chaos Mod config"); return 0; });
				else if (trigger.Command == "test_pause_game")
				{
					timed([&] { client.SendResult(trigger.Id, true, "pausing"); return 0; });
					pausedUntil = GameLinkClient::Now() + 4000;
				}
				else if (trigger.Command == "test_quit")
				{
					timed([&] { client.SendResult(trigger.Id, true, "bye"); return 0; });
					runSeconds = 0;
				}
				else
					timed([&] { client.SendResult(trigger.Id, false, "Unknown effect id \"" + trigger.Command + "\""); return 0; });
			}
		}

		if (frameUs > maxFrameUs)
			maxFrameUs = frameUs;
		frames++;

		Sleep(16); // ~60 fps
	}

	auto stopStart = std::chrono::steady_clock::now();
	client.Stop();
	auto stopMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stopStart).count();

	char summary[256];
	std::snprintf(summary, sizeof(summary), "SUMMARY frames=%llu maxFrameWorkUs=%.1f connects=%llu stopMs=%.1f",
	              static_cast<unsigned long long>(frames), maxFrameUs,
	              static_cast<unsigned long long>(client.GetConnectCount()), stopMs);
	Print(summary);
	return 0;
}
