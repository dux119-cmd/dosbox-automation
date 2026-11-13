<!--
This file is part of the dosbox-automation Project.
License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
-->

# 301 Lua scripting

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
    (Windows: Git Bash, MSYS2 or WSL). Release packages: at the package root,
    beside the binary; AppImage: `dosbox-automation-*.AppImage --dosboxctl ...`;
    source tree: `scripts/clients/dosboxctl`. Token from `DOSBOX_API_TOKEN`,
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

## Quick setup

Engine running, token in a file (101):

```sh
export DOSBOX_TOKEN_FILE=$PWD/token
A="Authorization: Bearer $(cat token)"; U=http://127.0.0.1:8386/api/v1
```

## When Lua, when the API

- Lua runs inside the engine, frame by frame: exact timing, no HTTP round
  trip between "screen shows X" and "press Y". For install scripts, input on
  a given frame, reading memory every frame.
- One HTTP call per step is enough for everything else (201).

## Load, start, status, stop

| Task | curl | dosboxctl |
|---|---|---|
| Load | `curl -s -H "$A" -X POST -H 'Content-Type: text/plain' --data-binary @s.lua "$U/script/load?name=demo"` | |
| Start | `curl -s -H "$A" -X POST $U/script/start` | |
| Load and start | | `dosboxctl file s.lua`, `dosboxctl run 'dosbox.output.v = 5'` |
| Status | `curl -s -H "$A" $U/script/status` | `dosboxctl script-status` (`--json` for the answer) |
| Stop | `curl -s -H "$A" -X POST $U/script/stop` | `dosboxctl script-stop` |

- Load body: the Lua source, `Content-Type: text/plain` or `application/lua`
  (else 415). At most 256 KB (413), not empty (400). A syntax error answers
  400 with the line: `[string "demo"]:1: unexpected symbol near 'then'`.
- Load query: `name` (letters, digits, `-`, `_`, at most 64), `seed`
  (integer: `math.random` repeats per seed), `debug=true` (trace file in
  `<config dir>/logs/lua-debug-<date>-<time>-<name>.log`).
- One load per 2 s: else 429, `Retry-After` the seconds left.
- One script at a time. Load or start while it runs: 400 `a script is already
  running`. Start with nothing loaded: 400 `no script loaded`.
- Start again after it ended: runs the same script again, no reload needed.
- Stop answers `stopped`, also when nothing runs.
- A load starts a fresh Lua state: globals of the last script are gone.

## Status

`{"state": "yielded", "frame": 448, "name": "demo", "output": {...}, "error": "..."}`

| state | means |
|---|---|
| `idle` | loaded and not started, or stopped |
| `running`, `yielded` | running; `yielded` while it waits for frames, text or typing |
| `completed` | ended normally; `output` stays readable |
| `error` | ended with `error`: the Lua message or a limit |

- `frame`: the engine's frame counter.
- `output`: the `dosbox.output` table as JSON. Numbers, strings, booleans,
  nested tables; a list comes back as an object with keys `"1"`, `"2"`;
  functions are dropped. Set values as you go: a stopped or failed script
  keeps what it wrote.

## Limits

| Limit | Value | Error |
|---|---|---|
| Lua instructions per run, waits included | 1,000,000 (a counting loop with a wait every 1000 rounds: ~166,000 rounds) | `instruction limit exceeded` |
| Time between two yields | 5 s | `wall-clock time limit exceeded` |
| `wait_for_text` or `type` while no frames come | 30 s | none: `wait_for_text` returns false, `type` goes on with keys undelivered |
| Memory | 16 MB | `not enough memory` |
| Lua pattern subject, pattern | 64 KB, 1024 chars | |

- Watching something for a long time: a script per phase, each start begins
  a fresh budget; or poll from outside with the API.
- Not there: `os`, `io`, `debug`, `package`, `load`, `loadfile`, `dofile`,
  `require`, `getmetatable`, `setmetatable`, `collectgarbage`, `rawget`,
  `rawset`, `rawlen`, `rawequal`, `string.dump`. There: `string`, `table`,
  `math`, `coroutine`, `utf8`, base functions.

## Functions (table `dosbox`)

| Function | Returns, notes |
|---|---|
| `wait_frames(n)` | yields n frames |
| `frame()` | frame number |
| `type(text)` | yields until typed; `\n` is Enter |
| `key(name, pressed)` | `KBD_` names as in 201: `dosbox.key("KBD_enter", true)` then `false` |
| `mouse_move(dx, dy)`, `mouse_click(button)` | `left`, `right`, `middle` |
| `screen_text()` | text-mode screen as one string |
| `screen_match(pattern [, {ignorecase = true}])` | bool; Lua pattern |
| `wait_for_text(pattern, frames [, {ignorecase = true}])` | yields; true when found, false at the frame timeout |
| `is_text_mode()` | bool |
| `mem_read(seg, off, len)` | string of bytes |
| `mem_read_byte(seg, off)`, `mem_read_word(seg, off)` | integer |
| `mem_write(seg, off, data)` | data: a string of bytes |
| `drive_swap(drive, image)` | done when it returns; policy as 103 |
| `mount_lock()` | as the API's mount lock (103) |
| `osd(text [, opts])`, `osd_clear()` | on the window only, never in recordings (204) |
| `capture_start([mode])`, `capture_stop()` | error while a capture runs (204) |
| `audio_capture_start([path])`, `audio_capture_stop()` | error while a capture runs (204) |
| `mixer_info()`, `mixer_volume(l, r)`, `mixer_channel_volume(name, l, r)`, `mixer_mute()`, `mixer_unmute()` | |
| `log(msg)` | engine log (`lua: msg`) and the debug trace |
| `debugmsg(msg)` | debug trace only (`debug=true` at load) |
| `abort(msg)` | ends the script with `error` |
| `output` | table read back by the status route |

- Lua patterns, not regular expressions: `%.` for a dot, `%d` for a digit,
  `C:\` is literal (`wait_for_text("C:\\>", 300)` in Lua source).
- One frame is about 1/70 s in text mode and mode 13h.

## A script that installs

```lua
dosbox.type("install\n")
if not dosbox.wait_for_text("Select drive", 700) then
  dosbox.abort("installer did not start")
end
dosbox.key("KBD_enter", true)
dosbox.key("KBD_enter", false)
dosbox.wait_frames(10)
dosbox.output.step = "drive chosen"
```

- Wait for a screen state, never for a fixed number of frames alone.
- Swaps for disk 2: `dosbox.drive_swap("a", "disk2.img")` in the script, or
  set `dosbox.output.need = 2`, poll status outside, swap through the API,
  and let the script wait for the next prompt.

More on input: 201. Memory: 203. Recording: 204.
