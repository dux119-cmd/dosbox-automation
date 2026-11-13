<!--
This file is part of the dosbox-automation Project.
License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
-->

# dosbox-automation: agent guide

Before any task with dosbox-automation: read the matching file from the index
below. Start with 101 if you have never run it.

## Minimum (same in every file)

- dosbox-automation: DOS emulator, controlled over an HTTP REST API.
- API off by default. Enable at start: `--set webserver_enabled=true`, or
  `webserver_enabled = true` in `[webserver]` of a conf. Not switchable at runtime.
- Base URL: `http://127.0.0.1:8386/api/v1/`. Port `webserver_port` (default 8386).
  Binds 127.0.0.1 only.
- Every `/api/v1/` call needs header `Authorization: Bearer <token>`. Else 401.
  - Preferred: env `DOSBOX_API_TOKEN=<64 hex chars>` set before start.
  - Or `--set webserver_token_file=true`: engine writes
    `<config dir>/webserver/api_token`.
  - Neither: random token, log shows 8 chars only. Unusable.
- Config dir: Linux, BSD `~/.config/dosbox-automation` (`$XDG_CONFIG_HOME`
  wins), macOS `~/Library/Preferences/dosbox-automation`, Windows
  `%LOCALAPPDATA%\dosbox-automation` (`%XDG_CONFIG_HOME%` wins when set).
- No token needed: `/` (start page), `/api.html` (Swagger UI), `/openapi.json`
  (full spec; source copy `resources/webserver/openapi.json`).
- Interfaces:
  - REST: curl or any HTTP client.
  - dosbox-mcp: MCP server. `pipx install dosbox-mcp` or `uvx dosbox-mcp`.
    Token from env `DOSBOX_API_TOKEN` or the token file.
  - dosboxctl: command-line client for every route. Needs bash, curl, python3
    (Windows: Git Bash, MSYS2 or WSL). Release packages: beside this file;
    AppImage: `dosbox-automation-*.AppImage --dosboxctl ...`; source tree:
    `scripts/clients/dosboxctl`. Token from `DOSBOX_API_TOKEN`,
    `DOSBOX_TOKEN_FILE` or the token file; port `DOSBOX_PORT`. `dosboxctl --help`.
- Stop: `POST /api/v1/dosbox/shutdown`. Fallback: kill your own PID. Never
  `pkill dosbox`: other sessions on the machine die with it.

```sh
TOKEN=$(python3 -c "import secrets; print(secrets.token_hex(32))")
DOSBOX_API_TOKEN=$TOKEN dosbox-automation --set webserver_enabled=true &
curl -s -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8386/api/v1/status
curl -s -X POST -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8386/api/v1/dosbox/shutdown
# the same with dosboxctl:
DOSBOX_API_TOKEN=$TOKEN dosboxctl status
DOSBOX_API_TOKEN=$TOKEN dosboxctl shutdown
```

## Index: question -> file (docs/agents/)

| Question | File |
|---|---|
| How do I set it up, start it, talk to it, stop it? | dosbox-automation-101-setup-running.md |
| How do I prepare a conf? Which settings for automated runs? | dosbox-automation-102-recommended-settings.md |
| How do I mount a folder, floppy, CD image? Swap disks? Why is MOUNT blocked? | dosbox-automation-103-mounting-guide.md |
| What are archive mounts? How do I run a game from zip, rar, 7z, iso? | dosbox-automation-104-working-with-archive-mounts.md |
| How do I type, press keys, click, read the screen, take a screenshot, read memory? | dosbox-automation-201-basic-usage.md |
| How do I install and start a game end to end? | dosbox-automation-202-how-to-run-a-game.md |
| How do I inspect or change registers, memory, ports? Search memory, freeze values? | dosbox-automation-203-how-to-debug-a-game.md |
| How do I record video, audio or input? | dosbox-automation-204-how-to-record-video-audio.md |
| How do I run a Lua script inside the engine? What are its limits? | dosbox-automation-301-lua-scripting.md |
| How do I record a person's input and replay it? Send a timed key sequence? | dosbox-automation-302-input-recording-replay.md |
| How do I find a game's value in memory (ammo, lives) and hold it? | dosbox-automation-303-memory-trainer.md |
| Where is route X, Lua function Y, setting Z? | dosbox-automation-601-reference-guide.md |
