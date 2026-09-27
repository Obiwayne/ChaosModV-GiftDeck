Chaos Mod V - GiftDeck build
============================

A build of Chaos Mod V (https://github.com/gta-chaos-mod/ChaosModV, GPL-3, by pongo1231 and contributors)
that lets GiftDeck start effects when TikTok LIVE gifts come in. Source: https://github.com/Obiwayne/ChaosModV-GiftDeck
(branch "giftdeck"). Based on upstream tag 2.2 (version 2.2-beta). Licensed GPL-3 like upstream (see
chaosmod/Licenses/LICENSE.ChaosModV.txt).

What it adds
------------
- A GameLink client inside the mod. Once you are in Story Mode it connects to GiftDeck at ws://127.0.0.1:21216/
  (retrying every 3 seconds while GiftDeck isn't running; it never blocks the game).
- On connect it sends every registered effect (id, name, category) so GiftDeck can list them.
- When GiftDeck sends a trigger, the effect starts right away (like the effect shortcuts / debug menu: no voting,
  no timer). The viewer's name is shown after the effect name, e.g. "Suicide (nickname)".
- GiftDeck gets an answer for every trigger: ok, or why not (unknown effect, disabled in the effects config,
  permanent effect, condition not met, not in game).

Everything else works as in the normal Chaos Mod: config app, shortcuts, Twitch voting, Lua scripts.
Turn the timer off with a huge NewEffectSpawnTime (GiftDeck's installer does this for you).

Optional settings (chaosmod/configs/config.json)
------------------------------------------------
  "EnableGiftDeckLink": false             turn the GiftDeck connection off
  "GiftDeckLinkPort": 21216               another GameLink port
  "GiftDeckShowUserInEffectName": false   don't show "(nickname)" after the effect name

Dumping the effect list
-----------------------
Create an empty file chaosmod/.giftdeckdumphello and the mod writes the hello message (all effects, including Lua
script effects) to chaosmod/giftdeck_hello.json when it starts.

Building
--------
  scripts\build-giftdeck.cmd            (Visual Studio 2026, MSVC x64, bundled CMake + Ninja)
  tests\giftdeck\run-tests.cmd          (GameLink protocol test + offline effect list dump; Python 3 + websockets)
