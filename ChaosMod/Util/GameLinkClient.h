#pragma once

// GiftDeck GameLink client (GiftDeck build of Chaos Mod V).
//
// Connects to GiftDeck's local WebSocket server (ws://127.0.0.1:21216/ by default) using WinHTTP's
// WebSocket API. Everything network related runs on background threads; the game thread only ever
// touches this class through non-blocking calls (SetHello, PopTrigger, SendResult, Heartbeat).
//
// Protocol (JSON text frames, one object per frame):
//   mod -> GiftDeck   {"type":"hello","game":"gta5","mod":"chaosmod","name":...,"version":...,"commands":[...]}
//   GiftDeck -> mod   {"type":"trigger","id":"<guid>","command":"<effect id>","args":{},"user":..,"gift":..,"count":1}
//   mod -> GiftDeck   {"type":"result","id":"<guid>","ok":true,"message":""}
//   mod -> GiftDeck   {"type":"status","text":"..."}
//   either            {"type":"ping"} / {"type":"pong"}   (keep-alive, every 15 s)
//
// This file has no dependency on the game so it can be unit tested on its own.

#include <json.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class GameLinkClient
{
  public:
	struct Trigger
	{
		std::string Id;
		std::string Command;
		nlohmann::json Args = nlohmann::json::object();
		std::string User;
		std::string Gift;
		int Count                  = 1;
		std::uint64_t ReceivedTick = 0;
	};

	struct Options
	{
		std::wstring Host                 = L"127.0.0.1";
		unsigned short Port               = 21216;
		std::wstring Path                 = L"/";
		std::uint32_t ReconnectDelayMs    = 3000;
		std::uint32_t PingIntervalMs      = 15000;
		// Connection is considered dead if nothing was received for this long
		std::uint32_t DeadTimeoutMs       = 45000;
		// Game thread must have called Heartbeat() within this window, else triggers are refused ("not in game")
		std::uint32_t HeartbeatTimeoutMs  = 2500;
		// Queued triggers not picked up by the game thread within this window are answered with ok=false
		// (GiftDeck expects a result within 5 s)
		std::uint32_t TriggerTimeoutMs    = 4000;
	};

	using LogFunc = std::function<void(const std::string &)>;

  private:
	Options m_Options;
	LogFunc m_Log;

	std::mutex m_Mutex; // guards everything below that isn't atomic
	std::condition_variable m_Cv;
	std::string m_HelloJson;
	bool m_HelloDirty = false;
	std::deque<std::string> m_Outbox;
	std::deque<Trigger> m_Triggers;
	std::string m_StatusText;
	std::string m_SentStatusText;

	std::atomic<bool> m_StopRequested { false };
	std::atomic<bool> m_Connected { false };
	std::atomic<bool> m_ReceiveFailed { false };
	std::atomic<std::uint64_t> m_LastHeartbeat { 0 };
	std::atomic<std::uint64_t> m_LastReceive { 0 };
	std::atomic<std::uint64_t> m_ConnectCount { 0 };

	std::thread m_Thread;
	void *m_Session   = nullptr;
	void *m_Connect   = nullptr;
	void *m_WebSocket = nullptr;

  public:
	explicit GameLinkClient(Options options = {}, LogFunc log = {});
	~GameLinkClient();

	GameLinkClient(const GameLinkClient &)            = delete;
	GameLinkClient &operator=(const GameLinkClient &) = delete;

	void Start();
	// Blocks until the background threads exit (at most ~1 s). Don't call from DllMain.
	void Stop();
	bool IsRunning() const
	{
		return m_Thread.joinable();
	}

	bool IsConnected() const
	{
		return m_Connected;
	}

	std::uint64_t GetConnectCount() const
	{
		return m_ConnectCount;
	}

	// Full hello message (JSON text). Sent on every (re)connect; resent right away if it changed while connected.
	void SetHello(const std::string &helloJson);

	// Game thread calls this every frame while the mod is running and able to start effects.
	void Heartbeat();

	// Optional status line shown by GiftDeck; only sent when it changes.
	void SetStatus(const std::string &text);

	bool PopTrigger(Trigger &outTrigger);
	void SendResult(const std::string &id, bool ok, const std::string &message);
	void Send(const nlohmann::json &message);

	static std::uint64_t Now();

  private:
	void Log(const std::string &text);
	void Run();
	bool ConnectOnce();
	void ReceiveLoop(void *webSocket);
	void CloseHandles();
	bool SendRaw(const std::string &text);
	void HandleMessage(const std::string &text);
	void FailStaleTriggers();
};
