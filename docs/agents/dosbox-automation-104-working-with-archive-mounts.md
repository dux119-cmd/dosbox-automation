<!--
This file is part of the dosbox-automation Project.
License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
-->

# 104 Working with archive mounts

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

## Setup for the commands below

Engine running with the API on and `$TOKEN` set (101). DOS commands are typed
with `dosboxctl type '<command>'` (201) or written into `[autoexec]`.

```sh
A="Authorization: Bearer $TOKEN"; U=http://127.0.0.1:8386/api/v1
J='Content-Type: application/json'
export DOSBOX_API_TOKEN=$TOKEN
```

## What an archive mount is

- `mount c game.zip` makes the archive a drive. Nothing is unpacked to the
  game folder; the archive file is never written.
- Writes go to a sidecar folder on the host. Delete the sidecar to reset the
  game.
- Same mount policy as disk images (103): the archive must be inside the conf
  anchor or `mount_allowed_image_roots`; `mount_allowed_bases` does not count.

| Task | DOS command |
|---|---|
| Archive as C: | `mount c game.zip -pr` |
| A folder inside it as the root | `mount c collection.zip -pr -root GAMES\KEEN4` |
| Read-only, no sidecar | `mount c game.zip -pr -ro` |
| Installer media, writes thrown away at unmount | `mount d setup.zip -pr -scratch` |
| Floppy image inside an archive | `mount a game.zip/disks/disk1.img -pr` |
| CD image inside an archive | `mount d game.zip/cd/game.iso -pr` |
| Start a program after the mount | `mount c game.zip -pr -run KEEN4E` |
| Start the one program, or open the menu | `mount c game.zip -pr -run` |

- One top-level folder in the archive: it becomes the root
  (`Using GAME as the root of the archive.`).
- `-t archive` forces the type when the extension is wrong.
- Swap to an image inside an archive: same path form, `"image":
  "game.zip/disks/disk2.img"` (103).

## Formats

| Format | Served |
|---|---|
| zip, RAR 4 (not solid) | file by file, in place |
| RAR 5, 7z, tar, tar.gz/bz2/xz/zst | unpacked into the archive cache once, at mount |
| RAR 4 solid, encrypted, multi-volume | refused |
| `.iso`, `.cue`+`.bin` inside an archive | from the cache, as a CD |
| `disk1.img.xz`, `.imz`, `.isz` | mounted as the one image inside |

- `archive_sequential = refuse` in `[dos]`: RAR 5, 7z and tar are refused
  instead of unpacked.
- Nested: `mount c collection.zip/keen4.zip` mounts the inner zip; up to three
  archives deep.

## Where writes go

- Sidecar: `<saves dir>/<archive name>-<8 hex of its hash>/`.
- Saves dir: Linux, BSD `~/.local/share/dosbox-automation/saves`
  (`$XDG_DATA_HOME`), Windows `%LOCALAPPDATA%\dosbox-automation\saves`, macOS
  `~/Library/Application Support/dosbox-automation/saves`. Setting
  `archive_sidecar_path` moves it; `-sidecar PATH` names one for a mount.
- A changed file is copied into the sidecar on the first write. Deletions and
  renames go to `.source-archive.toml` in the sidecar.
- A disk image from an archive is copied into the sidecar's `.images/` and
  written there.
- One mount per sidecar: a second engine mounting the same archive is refused
  (`-nosidecar` reads it read-only beside the first).
- The sidecar is keyed on the archive's contents: the same zip inside a
  collection (`collection.zip/game.zip`) shows the saves made with `game.zip`.

## Programs: -run and the menu

- `-run NAME`: 8.3 path on the new drive; `.COM`, `.EXE`, `.BAT` tried in that
  order. Not found: drive stays mounted, MOUNT lists the programs at the root.
- `-run` without a name: `CHOOSER.TML`, then `DOSBOX.BAT`, then the one program
  at the root, then the one program on the drive. Several: the program menu
  (UniMenu) opens in DOS.
- Menu keys: arrows and Enter start, Escape leaves it at the prompt.
- Agents that do not want the menu: `-run NAME`, or `--set unimenu=off` (MOUNT
  then lists the programs and stops at the prompt).
- `-exit` with `-run`: the engine quits when the program ends.

## Start an archive from the command line

```sh
DOSBOX_API_TOKEN=$TOKEN dosbox-automation game.zip --set webserver_enabled=true &
DOSBOX_API_TOKEN=$TOKEN dosbox-automation game.zip --run KEEN4E --set webserver_enabled=true &
```

- The archive becomes C:, its program runs, and **the engine quits when that
  program exits**, API on or not. To keep the engine for more work, start it
  with a conf in the archive's folder (`-conf`, the conf anchor) or with that
  folder in `mount_allowed_image_roots` of the primary config, then `mount c
  game.zip -pr -run NAME` inside. A bare start (`-noprimaryconf`, no `-conf`)
  has neither and refuses every mount.
- With the API on, the archive file itself counts as an allowed image root; no
  conf anchor needed.
- A DOSBox configuration inside the archive (`_DOSBOX/`, the root, GOG and
  Steam layouts) loads like a `-conf` file, after the primary conf and before
  your `-conf` files. It cannot set host-only settings such as `[webserver]`.
  `--set package_conf=off` switches that off.

## Cache and sidecars

| Task | DOS command | Host command |
|---|---|---|
| Show the cache | `mount -cache` | `dosbox-automation --archive-cache status` |
| Trim it | `mount -cache trim` | |
| Empty it | | `dosbox-automation --archive-cache empty` |
| List sidecars | | `dosbox-automation --sidecar list` |

- The cache holds only what can be unpacked again. Sidecars hold saves and are
  never deleted by the engine.

## While a mount works

- A long unpack holds the emulation: routes that need it answer 503 with
  `Retry-After: 1` until it is done (dosboxctl retries for 10 s).
- `GET /api/v1/status` carries `"archive": {"activity", "archive",
  "bytes_done", "bytes_total"}` while the archive helper works.

## Refusals

```
locked.zip
cannot be mounted: encrypted archive.
  encrypted entry
```

- Log line: `MOUNT: Blocked '<path>' - <reason>` (here `- encrypted archive:
  encrypted entry`). Archive reasons: `sequential
  archive format`, `solid RAR 4 archive`, `encrypted archive`, `multi-volume
  archive`, `unsupported compression method`, `archive over the configured
  caps`, `archive cannot be read`, `the sidecar is in use by another mount`.
