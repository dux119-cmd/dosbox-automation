<!--
This file is part of the dosbox-automation Project.
License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
-->

# 102 Recommended settings

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

## A conf for automated runs

`game/dosbox.conf` (anything left out keeps its default):

```ini
[sdl]
output = opengl

[cpu]
cpu_cycles = 12000

[dos]
keyboard_layout = us

[joystick]
joysticktype = none

[capture]
capture_dir = capture

[webserver]
webserver_enabled = true
webserver_token_file = true

[autoexec]
mount c c -pr
c:
```

| Line | Why |
|---|---|
| `output = opengl` | shaders (the CRT look) exist only with OpenGL; `texture` shows the plain picture |
| `cpu_cycles = 12000` | fixed speed, the same timing every run; `cycles` is the deprecated old name |
| `keyboard_layout = us` | API typing uses US key positions (201) |
| `joysticktype = none` | a plugged-in controller can stall the first API calls after start |
| `capture_dir = capture` | recordings next to the run; a relative path counts from this conf's folder |
| `webserver_token_file = true` | the token in a file for clients that start first (101) |

Start it with `-conf game/dosbox.conf`; add `-noprimaryconf -nolocalconf` when
nothing of the user's own setup may leak in.

## Where settings come from (later wins)

1. primary conf `<config dir>/dosbox-automation.conf` (skip: `-noprimaryconf`)
2. local `dosbox.conf` in the working directory (skip: `-nolocalconf`)
3. a game package's own conf (104)
4. each `-conf FILE`, in order
5. each `--set section.key=value` (or `--set key=value`)

- `mount_allowed_bases` and `mount_allowed_image_roots` count only from the
  primary conf file, read even under `-noprimaryconf` (103).
- A game package's conf cannot set host-only settings: `[webserver]`, serial
  ports, network forwards, paths on the host, archive limits.

## The webserver section

| Setting | Default | For agents |
|---|---|---|
| `webserver_enabled` | false | true; only at start |
| `webserver_port` | 8386 | another port per engine when several run |
| `webserver_token_file` | false | true when the client starts first (dosbox-mcp); file `<config dir>/webserver/api_token`, mode 0600, removed at a clean shutdown |
| `webserver_bind_address` | 127.0.0.1 | leave |
| `webserver_allow_remote` | false | leave: true exposes the API to the network |
| `webserver_osd` | true | the on-screen marker while the API drives the machine |
| `webserver_tool_token` | embed | tool pages only (Cheat Workbench) |

All of them take effect only at start. Inside DOS, `CONFIG -set` refuses them:
`The [webserver] settings can't be changed from inside DOS.`

## Headless

| Variable | Effect |
|---|---|
| `SDL_VIDEODRIVER=offscreen` | no window; frames, text and rendered capture still work (on macOS unshaded, see 101) |
| `SDL_AUDIODRIVER=dummy` | no sound device; audio capture still records |

## What changes at runtime and what does not

- Start only: `[webserver]`, `machine`, `memsize`, `output`, the mount roots.
- At runtime, without a restart: `PUT /api/v1/cpu/cycles` (fixed cycles),
  the mixer routes, `PUT /api/v1/capture/video/compression`.
- A game package mounted later names the start-only settings it could not
  apply (104).
