#include <stdafx.h>

#include "GameLinkClient.h"

#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

using nlohmann::json;

namespace
{
	constexpr std::uint32_t kReceiveBufferSize = 64 * 1024;
	constexpr std::size_t kMaxMessageSize      = 4 * 1024 * 1024;
	constexpr std::size_t kMaxQueuedTriggers   = 256;
	constexpr const char *kIdleStatus         = "Not in game (loading, paused or Chaos Mod disabled)";

	std::string JsonString(const json &obj, const char *key)
	{
		auto it = obj.find(key);
		if (it == obj.end() || it->is_null())
			return {};
		if (it->is_string())
			return it->get<std::string>();
		return it->dump();
	}
}

std::uint64_t GameLinkClient::Now()
{
	return GetTickCount64();
}

GameLinkClient::GameLinkClient(Options options, LogFunc log) : m_Options(std::move(options)), m_Log(std::move(log))
{
}

GameLinkClient::~GameLinkClient()
{
	Stop();
}

void GameLinkClient::Log(const std::string &text)
{
	if (m_Log)
		m_Log(text);
}

void GameLinkClient::Start()
{
	if (m_Thread.joinable())
		return;

	m_StopRequested = false;
	m_Thread        = std::thread([this]() { Run(); });
}

void GameLinkClient::Stop()
{
	if (!m_Thread.joinable())
		return;

	m_StopRequested = true;
	m_Cv.notify_all();
	m_Thread.join();
}

void GameLinkClient::SetHello(const std::string &helloJson)
{
	{
		std::lock_guard lock(m_Mutex);
		if (m_HelloJson == helloJson)
			return;
		m_HelloJson  = helloJson;
		m_HelloDirty = true;
	}
	m_Cv.notify_all();
}

void GameLinkClient::Heartbeat()
{
	m_LastHeartbeat = Now();
}

void GameLinkClient::SetStatus(const std::string &text)
{
	{
		std::lock_guard lock(m_Mutex);
		if (m_StatusText == text)
			return;
		m_StatusText = text;
	}
	m_Cv.notify_all();
}

bool GameLinkClient::PopTrigger(Trigger &outTrigger)
{
	std::lock_guard lock(m_Mutex);
	if (m_Triggers.empty())
		return false;

	outTrigger = std::move(m_Triggers.front());
	m_Triggers.pop_front();
	return true;
}

void GameLinkClient::SendResult(const std::string &id, bool ok, const std::string &message)
{
	Send(json { { "type", "result" }, { "id", id }, { "ok", ok }, { "message", message } });
}

void GameLinkClient::Send(const json &message)
{
	std::string text;
	try
	{
		// replace invalid UTF-8 (e.g. odd effect names) instead of throwing
		text = message.dump(-1, ' ', false, json::error_handler_t::replace);
	}
	catch (...)
	{
		return;
	}

	{
		std::lock_guard lock(m_Mutex);
		if (!m_Connected)
			return;
		m_Outbox.push_back(std::move(text));
	}
	m_Cv.notify_all();
}

void GameLinkClient::CloseHandles()
{
	if (m_WebSocket)
	{
		// Closing the handle aborts a pending WinHttpWebSocketReceive on the receive thread
		WinHttpCloseHandle(m_WebSocket);
		m_WebSocket = nullptr;
	}
	if (m_Connect)
	{
		WinHttpCloseHandle(m_Connect);
		m_Connect = nullptr;
	}
	if (m_Session)
	{
		WinHttpCloseHandle(m_Session);
		m_Session = nullptr;
	}
}

bool GameLinkClient::ConnectOnce()
{
	m_Session = WinHttpOpen(L"ChaosModV-GiftDeck/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
	                        WINHTTP_NO_PROXY_BYPASS, 0);
	if (!m_Session)
		return false;

	// resolve, connect, send, receive (ms). Receive timeout doubles as a dead-connection guard.
	WinHttpSetTimeouts(m_Session, 2000, 2000, 5000, static_cast<int>(m_Options.DeadTimeoutMs + 15000));

	m_Connect = WinHttpConnect(m_Session, m_Options.Host.c_str(), m_Options.Port, 0);
	if (!m_Connect)
		return false;

	HINTERNET request = WinHttpOpenRequest(m_Connect, L"GET", m_Options.Path.c_str(), nullptr, WINHTTP_NO_REFERER,
	                                       WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
	if (!request)
		return false;

	bool ok = WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)
	       && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
	       && WinHttpReceiveResponse(request, nullptr);

	if (ok)
	{
		DWORD status     = 0;
		DWORD statusSize = sizeof(status);
		WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
		if (status != 101)
		{
			Log("GameLink: server answered HTTP " + std::to_string(status) + " instead of a WebSocket upgrade");
			ok = false;
		}
	}

	if (ok)
		m_WebSocket = WinHttpWebSocketCompleteUpgrade(request, 0);

	WinHttpCloseHandle(request);

	return m_WebSocket != nullptr;
}

bool GameLinkClient::SendRaw(const std::string &text)
{
	if (!m_WebSocket)
		return false;

	DWORD error =
	    WinHttpWebSocketSend(m_WebSocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
	                         const_cast<char *>(text.data()), static_cast<DWORD>(text.size()));
	if (error != NO_ERROR)
	{
		Log("GameLink: send failed (error " + std::to_string(error) + ")");
		return false;
	}

	return true;
}

void GameLinkClient::ReceiveLoop(void *webSocket)
{
	std::string message;
	std::vector<char> buffer(kReceiveBufferSize);

	while (!m_StopRequested)
	{
		DWORD bytesRead                          = 0;
		WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType = {};
		DWORD error = WinHttpWebSocketReceive(webSocket, buffer.data(), static_cast<DWORD>(buffer.size()),
		                                      &bytesRead, &bufferType);
		if (error != NO_ERROR)
		{
			if (!m_StopRequested)
				Log("GameLink: connection lost (receive error " + std::to_string(error) + ")");
			break;
		}

		if (bufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
		{
			Log("GameLink: GiftDeck closed the connection");
			break;
		}

		m_LastReceive = Now();

		if (bufferType == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE
		    || bufferType == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE)
		{
			message.append(buffer.data(), bytesRead);
			if (message.size() > kMaxMessageSize)
			{
				Log("GameLink: message too large, dropping connection");
				break;
			}
			continue;
		}

		message.append(buffer.data(), bytesRead);
		if (bufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
			HandleMessage(message);
		message.clear();
	}

	m_ReceiveFailed = true;
	m_Cv.notify_all();
}

void GameLinkClient::HandleMessage(const std::string &text)
{
	json msg;
	try
	{
		msg = json::parse(text);
	}
	catch (const std::exception &)
	{
		Log("GameLink: ignoring invalid JSON message");
		return;
	}

	if (!msg.is_object())
		return;

	const auto type = JsonString(msg, "type");

	if (type == "ping")
	{
		Send(json { { "type", "pong" } });
	}
	else if (type == "trigger")
	{
		Trigger trigger;
		trigger.Id      = JsonString(msg, "id");
		trigger.Command = JsonString(msg, "command");
		trigger.User    = JsonString(msg, "user");
		trigger.Gift    = JsonString(msg, "gift");
		if (auto it = msg.find("args"); it != msg.end() && it->is_object())
			trigger.Args = *it;
		if (auto it = msg.find("count"); it != msg.end() && it->is_number_integer())
			trigger.Count = it->get<int>();
		trigger.ReceivedTick = Now();

		if (trigger.Command.empty())
		{
			SendResult(trigger.Id, false, "Missing command");
			return;
		}

		if (Now() - m_LastHeartbeat > m_Options.HeartbeatTimeoutMs)
		{
			SendResult(trigger.Id, false,
			           "Not in game (Chaos Mod is not running: loading screen, paused or mod disabled)");
			return;
		}

		bool full = false;
		{
			std::lock_guard lock(m_Mutex);
			if (m_Triggers.size() >= kMaxQueuedTriggers)
				full = true;
			else
				m_Triggers.push_back(std::move(trigger));
		}
		if (full)
			SendResult(JsonString(msg, "id"), false, "Too many queued triggers");
	}
	// "pong", "hello"/"welcome" or anything else from the server: nothing to do (m_LastReceive already updated)
}

void GameLinkClient::FailStaleTriggers()
{
	std::vector<std::string> staleIds;
	{
		std::lock_guard lock(m_Mutex);
		const auto now = Now();
		while (!m_Triggers.empty() && now - m_Triggers.front().ReceivedTick > m_Options.TriggerTimeoutMs)
		{
			staleIds.push_back(m_Triggers.front().Id);
			m_Triggers.pop_front();
		}
	}

	for (const auto &id : staleIds)
		SendResult(id, false, "Not in game (the game did not pick up the effect in time)");
}

void GameLinkClient::Run()
{
	bool loggedWaiting = false;

	while (!m_StopRequested)
	{
		if (!ConnectOnce())
		{
			CloseHandles();

			if (!loggedWaiting)
			{
				Log("GameLink: GiftDeck not reachable, retrying every "
				    + std::to_string(m_Options.ReconnectDelayMs / 1000) + " s");
				loggedWaiting = true;
			}

			std::unique_lock lock(m_Mutex);
			m_Cv.wait_for(lock, std::chrono::milliseconds(m_Options.ReconnectDelayMs),
			              [this]() { return m_StopRequested.load(); });
			continue;
		}

		loggedWaiting = false;
		m_ConnectCount++;
		m_LastReceive   = Now();
		m_ReceiveFailed = false;
		{
			std::lock_guard lock(m_Mutex);
			m_Outbox.clear();
			m_Triggers.clear();
			m_HelloDirty     = true;
			m_SentStatusText = {};
			m_Connected      = true;
		}
		Log("GameLink: connected to GiftDeck");

		std::thread receiveThread([this, webSocket = m_WebSocket]() { ReceiveLoop(webSocket); });

		auto lastPing = Now();
		bool alive    = true;
		while (alive && !m_StopRequested && !m_ReceiveFailed)
		{
			std::string hello;
			std::string status;
			std::deque<std::string> outbox;
			{
				std::unique_lock lock(m_Mutex);
				m_Cv.wait_for(lock, std::chrono::milliseconds(250),
				              [this]()
				              {
					              return m_StopRequested || m_ReceiveFailed || m_HelloDirty || !m_Outbox.empty()
					                  || m_StatusText != m_SentStatusText;
				              });

				if (m_HelloDirty && !m_HelloJson.empty())
				{
					hello        = m_HelloJson;
					m_HelloDirty = false;
				}
				// While the game thread isn't ticking (loading screen, pause menu with mod paused, Ctrl+L) report that
				const bool gameIdle = Now() - m_LastHeartbeat > m_Options.HeartbeatTimeoutMs;
				const auto &effectiveStatus =
				    gameIdle || m_StatusText.empty() ? std::string(kIdleStatus) : m_StatusText;
				if (effectiveStatus != m_SentStatusText)
				{
					status           = effectiveStatus;
					m_SentStatusText = effectiveStatus;
				}
				outbox.swap(m_Outbox);
			}

			if (m_StopRequested || m_ReceiveFailed)
				break;

			// hello always goes first so GiftDeck knows the catalog before anything else
			if (!hello.empty())
				alive = SendRaw(hello);
			if (alive && !status.empty())
				alive = SendRaw(json { { "type", "status" }, { "text", status } }.dump(
				    -1, ' ', false, json::error_handler_t::replace));
			for (const auto &text : outbox)
				if (alive)
					alive = SendRaw(text);

			const auto now = Now();
			if (alive && now - lastPing >= m_Options.PingIntervalMs)
			{
				lastPing = now;
				alive    = SendRaw(R"({"type":"ping"})");
			}

			if (alive && now - m_LastReceive > m_Options.DeadTimeoutMs)
			{
				Log("GameLink: no data from GiftDeck for too long, reconnecting");
				alive = false;
			}

			FailStaleTriggers();
		}

		{
			std::lock_guard lock(m_Mutex);
			m_Connected = false;
			m_Outbox.clear();
			m_Triggers.clear();
		}

		if (m_WebSocket && !m_ReceiveFailed && !m_StopRequested)
			Log("GameLink: disconnecting");

		// Graceful close is best effort; closing the handle below aborts the pending receive either way
		if (m_WebSocket && m_StopRequested)
			WinHttpWebSocketShutdown(m_WebSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);

		CloseHandles();
		receiveThread.join();

		if (!m_StopRequested)
		{
			Log("GameLink: disconnected from GiftDeck");
			std::unique_lock lock(m_Mutex);
			m_Cv.wait_for(lock, std::chrono::milliseconds(m_Options.ReconnectDelayMs),
			              [this]() { return m_StopRequested.load(); });
		}
	}

	CloseHandles();
}
