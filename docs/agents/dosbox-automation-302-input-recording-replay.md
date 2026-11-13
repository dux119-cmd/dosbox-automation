<!--
This file is part of the dosbox-automation Project.
License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
-->

# 302 Input recording and replay

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

## What it is for

- Record what a person does at the engine's window once, replay it later
  through the API: an install, a game's opening moves.
- Or write a sequence by hand: keys and mouse with exact delays, in one call.
- Input sent through the API or Lua is never recorded: only host input.

## Record

| Task | curl | dosboxctl |
|---|---|---|
| Start | `curl -s -H "$A" -X POST $U/input/record/start` | `dosboxctl input-record start` |
| Pause, resume (toggle) | `curl -s -H "$A" -X POST $U/input/record/pause` | `dosboxctl input-record pause` |
| Status | `curl -s -H "$A" $U/input/record/status` | `dosboxctl input-record status` |
| Stop, keep the events | `curl -s -H "$A" -X POST $U/input/record/stop > rec.json` | `dosboxctl --json input-record stop > rec.json` |

- Pause: `{"status": "paused"}`, again: `{"status": "recording"}`. Input while
  paused is not recorded.
- Status: `{"recording": true, "paused": false, "event_count": 20, "duration_ms": 1864.4}`.
- While recording, `PUT cpu/cycles` answers 409: the speed must stay as
  recorded.
- Mouse: recorded only while the engine passes the host mouse to the guest
  (captured, or seamless with a mouse driver). At a plain DOS prompt it does
  not; the click that captures the mouse is not passed on either.
- Errors: start while recording 409 `Already recording`; pause or stop with
  nothing recording 409 `Not recording`.

## Events

Stop answers `{"event_count": 36, "duration_ms": 2108.0, "events": [...]}`.
`duration_ms` here is the last event's `t`; in the status it is the time since start.

| type | fields |
|---|---|
| `key` | `key` (`KBD_` name, 201), `pressed` (default true) |
| `mouse_move` | `x_rel`, `y_rel`, `x_abs`, `y_abs` |
| `mouse_button` | `button` (`left`, `right`, `middle`), `pressed` |
| `mouse_wheel` | `delta` |
| every type | `t` (ms since start, emulated time), `frame` (frames since start), or `delay_ms` (ms after the previous event) |

A recorded event: `{"type": "key", "key": "KBD_e", "pressed": true, "t": 6.0, "frame": 1}`.

## Replay

```sh
python3 -c 'import json; d = json.load(open("rec.json")); json.dump({"events": d["events"]}, open("replay.json", "w"))'
curl -s -H "$A" -X POST -H 'Content-Type: application/json' --data-binary @replay.json $U/input/sequence
```

- Answer: `{"events_scheduled": 36, "status": "ok"}` at once; the events play
  over the following frames.
- With `frame` on any event: played by frame, shifted so the first event comes
  at the next frame. Without: played by `t` / `delay_ms` in emulated time.
- Keys wait while the guest's keyboard buffer is full; at most 500 events per
  frame.
- No route reports progress. Wait about `duration_ms` (36 events over 2.1 s
  took 147 frames), then check the screen. The engine log says
  `replay: (frame) complete: 36/36 events`.
- One replay at a time: 409 `Replay already in progress`. `input/type` answers
  the same 409 during a timed replay, but during a frame replay it is accepted
  and its keys mix into the replay (`ercehm ox one`). Send nothing else until
  it is done.
- Exact only from the same state: same conf, same cycles, the same program at
  the same point as when the recording started.

## By hand

```json
{"events": [
  {"type": "key", "key": "KBD_d", "pressed": true},
  {"type": "key", "key": "KBD_d", "pressed": false, "delay_ms": 50},
  {"type": "key", "key": "KBD_i", "pressed": true, "delay_ms": 50},
  {"type": "key", "key": "KBD_i", "pressed": false, "delay_ms": 50},
  {"type": "key", "key": "KBD_r", "pressed": true, "delay_ms": 50},
  {"type": "key", "key": "KBD_r", "pressed": false, "delay_ms": 50},
  {"type": "key", "key": "KBD_enter", "pressed": true, "delay_ms": 50},
  {"type": "key", "key": "KBD_enter", "pressed": false, "delay_ms": 50}
]}
```

- Post it like a replay; it types `dir` and Enter. `dosboxctl keys` does the
  same for keys (201).
- Same event, `t` or `delay_ms`, not both: 400.

| Answer | Cause |
|---|---|
| 400 `Missing or invalid 'events' array` | no `events` list |
| 400 `Too many events (max 32000)` | more than 32,000 |
| 400 `Unknown field 'x' for mouse_move event. Allowed fields: ...` | a misspelled field |
| 400 `Unknown event type: joystick` | a type not in the table |
| 400 `Unknown key: ...`, `Unknown button: ...` | name not in the tables |
| 400 `'delay_ms' must not be negative`, `'t' must not be negative` | |
| 409 `Replay already in progress` | a replay of the same kind runs |

Frame-exact input that reacts to the screen: Lua (301).
