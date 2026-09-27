"""
GameLink protocol test: plays the GiftDeck side (ws://127.0.0.1:<port>/) against gamelink_test.exe, which wraps the
real ChaosMod/Util/GameLinkClient.cpp with a fake game thread.

    python test_gamelink.py <path to gamelink_test.exe> [port]

Checks: reconnect loop while the server is down, hello framing, ping/pong both ways, trigger -> result for ok /
disabled / unknown / missing command, a >64 KB trigger (fragmented receive), "not in game" while the game thread is
not ticking, reconnect + hello resend after the server drops the connection, and that the fake game thread never
blocks on the client.
"""

import asyncio
import json
import subprocess
import sys
import time
import uuid

import websockets

EXE = sys.argv[1]
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 21299

passed = []
failed = []


def check(name, cond, detail=""):
    (passed if cond else failed).append(name)
    print(("PASS " if cond else "FAIL ") + name + ((" -- " + detail) if detail else ""), flush=True)


class Conn:
    """Collects messages from one client connection."""

    def __init__(self, ws):
        self.ws = ws
        self.queue = asyncio.Queue()
        self.all = []
        self.task = asyncio.create_task(self._reader())

    async def _reader(self):
        try:
            async for raw in self.ws:
                check_frame = isinstance(raw, str)
                msg = json.loads(raw)
                msg["_text_frame"] = check_frame
                self.all.append(msg)
                await self.queue.put(msg)
        except websockets.ConnectionClosed:
            pass

    async def expect(self, pred, timeout=5.0, what=""):
        deadline = time.monotonic() + timeout
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                raise TimeoutError("timed out waiting for " + what)
            msg = await asyncio.wait_for(self.queue.get(), left)
            if pred(msg):
                return msg

    async def trigger(self, command, **extra):
        tid = str(uuid.uuid4())
        msg = {"type": "trigger", "id": tid, "command": command, "args": {}, "user": "tester", "gift": "Rose",
               "count": 1}
        msg.update(extra)
        await self.ws.send(json.dumps(msg))
        return await self.expect(lambda m: m.get("type") == "result" and m.get("id") == tid, 5.0,
                                 "result for " + command)


async def main():
    proc = subprocess.Popen([EXE, str(PORT), "40"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    async def pump_output():
        loop = asyncio.get_running_loop()
        while True:
            line = await loop.run_in_executor(None, proc.stdout.readline)
            if not line:
                break
            print("   " + line.rstrip(), flush=True)
            if "SUMMARY" in line:
                summary.append(line)

    summary = []
    out_task = asyncio.create_task(pump_output())

    # 1) server not running yet: client must keep retrying without crashing
    await asyncio.sleep(2.5)
    check("client alive while GiftDeck is down", proc.poll() is None)

    conns = asyncio.Queue()

    async def handler(ws):
        c = Conn(ws)
        await conns.put(c)
        await c.task

    t_server = time.monotonic()
    server = await websockets.serve(handler, "127.0.0.1", PORT, max_size=None)

    c1 = await asyncio.wait_for(conns.get(), 5)
    check("reconnects within retry delay after GiftDeck starts", time.monotonic() - t_server < 3.0,
          "%.2fs" % (time.monotonic() - t_server))

    hello = await c1.expect(lambda m: True, 3, "first message")
    check("first frame is hello", hello.get("type") == "hello", str(hello.get("type")))
    check("hello is a text frame", hello["_text_frame"])
    check("hello fields", hello.get("game") == "gta5" and hello.get("mod") == "chaosmod"
          and hello.get("name") == "Chaos Mod V" and isinstance(hello.get("version"), str))
    cmds = hello.get("commands", [])
    check("hello commands shape", len(cmds) == 2 and all(
        set(c.keys()) == {"id", "name", "category", "description", "args"} and c["args"] == [] for c in cmds))
    check("hello utf-8 name survives", cmds[1]["name"] == "Disabled Thing é", cmds[1]["name"])

    status = await c1.expect(lambda m: m.get("type") == "status", 3, "status")
    check("status message", status.get("text") == "In game", str(status))

    # ping from GiftDeck -> pong
    await c1.ws.send(json.dumps({"type": "ping"}))
    await c1.expect(lambda m: m.get("type") == "pong", 3, "pong")
    check("answers ping with pong", True)

    # client keep-alive ping (test harness uses a 1.5 s interval; the mod uses 15 s)
    await c1.expect(lambda m: m.get("type") == "ping", 4, "client ping")
    check("client sends keep-alive ping", True)

    r = await c1.trigger("player_suicide")
    check("trigger known effect -> ok", r["ok"] is True and "Suicide" in r["message"], json.dumps(r))
    check("result has exactly type/id/ok/message",
          set(k for k in r if not k.startswith("_")) == {"type", "id", "ok", "message"})

    r = await c1.trigger("misc_disabled")
    check("trigger disabled effect -> ok=false", r["ok"] is False and "disabled" in r["message"], r["message"])

    r = await c1.trigger("nope_not_an_effect")
    check("trigger unknown effect -> ok=false", r["ok"] is False and "Unknown" in r["message"], r["message"])

    r = await c1.trigger("")
    check("trigger without command -> ok=false", r["ok"] is False, r["message"])

    big = "x" * 200_000
    r = await c1.trigger("player_suicide", args={"blob": big}, user="élève \U0001F600")
    check("200 KB trigger (fragmented receive) -> ok", r["ok"] is True)

    # burst of triggers in one go, all answered in order-independent way
    ids = []
    for i in range(20):
        tid = "burst-%d" % i
        ids.append(tid)
        await c1.ws.send(json.dumps({"type": "trigger", "id": tid, "command": "player_suicide", "args": {},
                                     "user": "u%d" % i, "gift": "Rose", "count": i + 1}))
    got = set()
    while len(got) < len(ids):
        m = await c1.expect(lambda m: m.get("type") == "result" and m["id"] in ids, 5, "burst results")
        got.add(m["id"])
    check("burst of 20 triggers all answered", len(got) == 20)

    # game thread stops ticking (loading screen / mod paused) -> "not in game"
    r = await c1.trigger("test_pause_game")
    await asyncio.sleep(3.0)
    r = await c1.trigger("player_suicide")
    check("trigger while game not ticking -> ok=false not in game",
          r["ok"] is False and "Not in game" in r["message"], r["message"])
    await asyncio.sleep(1.5)
    r = await c1.trigger("player_suicide")
    check("trigger after game resumes -> ok", r["ok"] is True, r["message"])

    # GiftDeck drops the connection -> client reconnects and sends hello again
    t_drop = time.monotonic()
    await c1.ws.close()
    c2 = await asyncio.wait_for(conns.get(), 5)
    hello2 = await c2.expect(lambda m: True, 3, "hello after reconnect")
    check("reconnect after server closes + hello resent", hello2.get("type") == "hello",
          "%.2fs" % (time.monotonic() - t_drop))

    # GiftDeck process disappears (server stops) -> client waits and comes back when it restarts
    server.close()
    await server.wait_closed()
    await asyncio.sleep(2.0)
    check("client alive after server shutdown", proc.poll() is None)
    server = await websockets.serve(handler, "127.0.0.1", PORT, max_size=None)
    c3 = await asyncio.wait_for(conns.get(), 5)
    hello3 = await c3.expect(lambda m: True, 3, "hello after server restart")
    check("reconnect after server restart + hello resent", hello3.get("type") == "hello")

    r = await c3.trigger("test_quit")
    await asyncio.get_running_loop().run_in_executor(None, proc.wait, 10)
    await out_task
    server.close()
    await server.wait_closed()

    check("harness exited cleanly", proc.returncode == 0, str(proc.returncode))
    if summary:
        parts = dict(p.split("=") for p in summary[0].split("SUMMARY")[1].split())
        check("game thread never blocked (max per-frame client work < 2 ms)", float(parts["maxFrameWorkUs"]) < 2000,
              parts["maxFrameWorkUs"] + " us")
        check("Stop() returns quickly (< 1.5 s)", float(parts["stopMs"]) < 1500, parts["stopMs"] + " ms")
        check("connected 3 times", parts["connects"] == "3", parts["connects"])

    print("\n%d passed, %d failed" % (len(passed), len(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
