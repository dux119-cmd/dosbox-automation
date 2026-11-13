<!--
This file is part of the dosbox-automation Project.
License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
-->

# 101 Quick start: set up, start, talk, stop

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

## 1. Prepare a folder

```
game/
  dosbox.conf
  c/            -> DOS drive C:
```

`game/dosbox.conf`:

```ini
[autoexec]
mount c c -pr
c:
```

- `-pr`: path relative to the folder of the last loaded conf.
- With the API on, MOUNT accepts only paths below:
  - the folder of the last `-conf` file; without one, a `dosbox.conf` in the
    working folder (never the primary conf's folder, never one at, inside or
    above the engine's config, data or cache folder, or at or above a portable
    program folder), or
  - the archive or game folder named on the command line (104), or
  - for folders only, the engine's `drives` folder: a `drives` folder in the
    folder the engine starts in, else the bundled one (the first found), or
  - `mount_allowed_bases` / `mount_allowed_image_roots` in `[webserver]` of the
    primary conf (`<config dir>/dosbox-automation.conf`). Read even under
    `-noprimaryconf`.
- Always refused: system folders (`/etc`, `/proc`, `C:\Windows`, Windows temp
  folders), paths with a symlink in them.
- Refusal on the DOS screen: `<path>` and `was refused by the mount policy:
  <reason>.` Log line: `MOUNT: Blocked '<path>' - <reason>`. Details: 103.

## 2. Start

```sh
TOKEN=$(python3 -c "import secrets; print(secrets.token_hex(32))")
DOSBOX_API_TOKEN=$TOKEN dosbox-automation -conf game/dosbox.conf \
    --set webserver_enabled=true > dosbox.log 2>&1 &
PID=$!
```

- No window: env `SDL_VIDEODRIVER=offscreen`. No sound: `SDL_AUDIODRIVER=dummy`.
  On macOS headless runs without OpenGL: the log shows the OpenGL renderer
  failing and the 'software' SDL renderer taking over; rendered frames are
  unshaded.
- Only your own settings: add `-noprimaryconf -nolocalconf`.
- Windows: `dosbox-automation.exe`; token via `set DOSBOX_API_TOKEN=...` (cmd)
  or `$env:DOSBOX_API_TOKEN="..."` (PowerShell).
- The binary: in a release package `./dosbox-automation` at its root; in a
  source tree `build/<preset>/dosbox-automation` (`build/release-linux/...`).
  A file named `dosbox` beside it is a leftover from before the rename: an old
  engine, do not run it.

### From an agent (every tool call is a new shell)

Variables and `&` jobs do not survive the call. Keep the token in a file and
detach the engine:

```sh
python3 -c "import secrets; print(secrets.token_hex(32))" > token; chmod 600 token
DOSBOX_API_TOKEN=$(cat token) setsid nohup dosbox-automation -conf game/dosbox.conf \
    --set webserver_enabled=true > dosbox.log 2>&1 < /dev/null &
echo $! > dosbox.pid
```

- In every later call: `export DOSBOX_TOKEN_FILE=$PWD/token` (dosboxctl reads
  it), or curl with `-H "Authorization: Bearer $(cat token)"`.
- A plain `&` without `setsid nohup` can die when the call's shell ends.
- macOS has no `setsid`: start with `nohup dosbox-automation ... > dosbox.log
  2>&1 < /dev/null & disown`. A missing setsid fails silently behind the `&`,
  and dosbox.pid then holds a dead PID.
- dosbox-mcp starts with your session, before the engine. It knows only the
  `DOSBOX_API_TOKEN` it had at its own start, else it reads a token file when
  it connects: the file `DOSBOX_TOKEN_FILE` in its environment names, else
  the one its `setup --token-file` names, else
  `~/.config/dosbox-automation/webserver/api_token` on every system. A
  variable set later never reaches it. For an engine you start later that the
  bridge must reach: start it without `DOSBOX_API_TOKEN` and with
  `--set webserver_token_file=true`; with the variable set, the engine writes
  no token file. Only while no other engine with a token file runs: one
  running engine owns the file, and a second one replaces its token.

## 3. Wait until ready

- Poll `GET /api/v1/status` until 200. Connection refused: not up yet, API off,
  or wrong port.
- DOS prompt reached: `"is_shell": true`.

```json
{"canonical_name": "", "is_booted": false, "is_shell": true, "program": "",
 "running": true, "shutdown_requested": false}
```

## 4. Talk to it

```sh
A="Authorization: Bearer $TOKEN"; U=http://127.0.0.1:8386/api/v1
```

| Task | Call | Answer |
|---|---|---|
| Type a command | `curl -s -H "$A" -X POST -H 'Content-Type: application/json' -d '{"text": "dir\n"}' $U/input/type` | `{"chars": 4, "status": "ok"}`; `\n` = Enter |
| Read the screen | `curl -s -H "$A" $U/video/text` | `{"text", "rows", "columns", "bios_mode", "is_text_mode", "page"}`; text modes only |
| Screenshot | `curl -s -H "$A" -o shot.png "$U/video/frame?format=png"` | PNG; `mode=raw` (default, framebuffer) or `mode=rendered` |
| Read memory | `curl -s -H "$A" $U/memory/0x40/0x10/8` | raw bytes (segment 0x40, offset 0x10, 8 bytes) |
| Read memory as JSON | same, plus `-H 'Accept: application/json'` | `{"memory": {"addr", "data": base64}, "registers"}` |
| What is running | `curl -s -H "$A" $U/program/state` | `{"canonical_name", "is_booted", "is_shell", "segment_name"}` |
| Version, features | `curl -s -H "$A" $U/dosbox/info` | `{"version", "features": {...}}` |

Keys, mouse, disk swap, Lua: 201 and 103.

## 5. Stop

```sh
curl -s -H "$A" -X POST $U/dosbox/shutdown   # {"status": "shutdown_requested"}
wait $PID || kill $PID
```

## Errors

| Answer | Meaning | Do |
|---|---|---|
| 401 | token missing or wrong | check header and `DOSBOX_API_TOKEN` |
| 503 + `Retry-After: 1` | emulation busy, command not run | send again after 1 s |
| 503 on `/video/frame` | no frame drawn yet after start | send again |
| 409 | conflict: input already running, or address not mapped under paging | read the JSON `error` |
| connection refused | not started, API off, wrong port | read `dosbox.log` |

## Next

- Conf settings: 102. Mounting: 103. Archives: 104.
- Input and screen in depth: 201. Whole game run: 202.
- Every route: `/openapi.json` or 601.
