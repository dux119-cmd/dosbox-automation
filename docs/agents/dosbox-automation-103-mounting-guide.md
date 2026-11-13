<!--
This file is part of the dosbox-automation Project.
License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
-->

# 103 Mounting guide

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
with `dosboxctl type '<command>'` or POST `/input/type` (201), or written into
`[autoexec]` of the conf.

```sh
A="Authorization: Bearer $TOKEN"; U=http://127.0.0.1:8386/api/v1
J='Content-Type: application/json'
export DOSBOX_API_TOKEN=$TOKEN
```

Example layout (conf anchor = `game/`):

```
game/
  dosbox.conf          started with -conf game/dosbox.conf
  c/                   folder for drive C:
  disks/disk1.img      floppy images
  disks/disk2.img
  cd/game.iso          CD image
```

## MOUNT: folders and images

| Task | DOS command |
|---|---|
| Folder as C: | `mount c c -pr` |
| Floppy image as A: | `mount a disks/disk1.img -t floppy -pr` |
| Several floppies on A: (swap later) | `mount a disks/disk1.img disks/disk2.img -t floppy -pr` |
| CD image as D: | `mount d cd/game.iso -t iso -pr` |
| Hard-disk image | `mount f hdd.img -t hdd -pr` |
| Label, read-only | `mount e c -pr -label GAMES -ro` |
| Unmount | `mount -u a` |
| List drives | `mount` |

- `-pr`: path relative to the folder of the last loaded conf. Without `-pr` a
  relative path counts from the engine's working directory. Absolute paths work
  too.
- An image can also be named as a DOS path on a mounted drive (`C:\GAME.ISO`).
  A relative name that exists on the host wins over the DOS reading.
- `-t`: `dir`, `overlay`, `floppy`, `hdd`, `iso` (or `cdrom`), `archive` (104).
  Without `-t` the type follows the file.
- Images: raw FAT images (floppy 160K to 2.88M, hard disk), ISO, CUE+BIN. Also
  `-fs none` (bootable, no DOS filesystem), `-chs C,H,S`, `-ide`; `mount /?`
  lists all.
- `-ro`: writes answer `Access denied - 'e:\new.txt'`.
- Success lines: `Local directory game/c/ mounted as E drive`,
  `FAT image <path> mounted as A drive`, `CD-ROM image <path> mounted as D drive`.
- `IMGMOUNT` still runs, as MOUNT: it prints `The IMGMOUNT command is
  deprecated; please use MOUNT instead.` and goes on as MOUNT with the same
  arguments. Write MOUNT.
- Mount the same image on one drive only.

Mount at start, before any API call: put the lines in `[autoexec]`:

```ini
[autoexec]
mount c c -pr
mount a disks/disk1.img disks/disk2.img -t floppy -pr
c:
```

## Make an empty image

| Task | DOS command |
|---|---|
| Floppy | `makeimg game/disks/blank.img -t fd_1440kb -label BLANK` |
| 20 MB hard disk, FAT16 | `makeimg game/hdd.img -t hd_20mb -label HDD` |

- Path on the host, relative to the engine's working directory.
- Asks `Proceed? (Y/N)`: answer with `dosboxctl type y`.
- Sizes: `fd_160kb` .. `fd_2880kb`, `hd_20mb` .. `hd_2gb`, or `-t hd -size <MB>`.

## Swap a disk (multi-floppy installs, CD changes)

| Task | curl | dosboxctl |
|---|---|---|
| Put disk 2 into A: | `curl -s -H "$A" -H "$J" -X POST -d '{"drive": "a", "image": "disks/disk2.img"}' $U/drive/swap` | `dosboxctl swap a disks/disk2.img` |

- Answer: `{"drive": "A", "status": "ok"}`. Check with `vol a:`.
- Relative image paths count from the conf anchor, then the image roots.
- Any allowed image works: one from the list of a multi-floppy mount, or one
  that was never in it. The swap route is the agent's way to change disks;
  Ctrl+F4 cycles a mounted list only from the host keyboard, injected through
  the input API it reaches the DOS program and changes nothing.
- The image must be inside the conf anchor or `mount_allowed_image_roots`.
- Every refusal answers 400 `{"error": "Blocked by mount policy"}`: outside the
  roots and a missing file look the same. Check the path yourself.
- Swap the image the program asks for when it asks ("Insert disk 2"), then press
  the key it wants.
- Locked: 403 `{"error": "mount is locked"}`.

## Mount policy (API on)

With the API on, MOUNT and swap reach only:
1. the conf anchor: the folder of the last `-conf` file, without one a
   `dosbox.conf` in the working folder; never the primary conf's folder, and
   none when that folder is, holds or lies inside the engine's config, data or
   cache folder, or in a portable layout is or holds the program folder,
2. the archive or game folder named on the engine's command line (104),
3. for folders only, the engine's `drives` folder: a `drives` folder in the
   folder the engine starts in, else the bundled one (the first found),
4. `mount_allowed_bases` (folders) and `mount_allowed_image_roots` (images) in
   `[webserver]` of the primary conf `<config dir>/dosbox-automation.conf`.
   Read even under `-noprimaryconf`. Several paths separated by `;`:

```ini
[webserver]
mount_allowed_bases = /home/me/dosgames;/data/more
mount_allowed_image_roots = /home/me/dosgames/disks
```

Always refused, API on or off: system folders (`/etc`, `/proc`, `C:\Windows`,
Windows temp folders), any path with a symlink in it. With the API on, also the
engine's own config, data and cache folders and everything inside them, apart
from its `drives` folder (in a portable layout the config folder is the program
folder and stays open to `mount_allowed_bases`), and CONFIG `-wc`, `-wcd` and
`-wl` (no host file writes from DOS).

Refusals on the DOS screen:

```
game/../elsewhere
was refused by the mount policy: outside allowed directories.
Put a dosbox-automation.conf next to the game and start with -conf,
or add the path to mount_allowed_bases in the primary config.
The security page of the manual explains the policy.
```

A disk image or archive is checked against `mount_allowed_image_roots`, not
`mount_allowed_bases`, and the refusal says so:

```
/home/me/elsewhere/disk1.img
was refused by the mount policy: outside allowed image roots.
Put a dosbox-automation.conf next to the game and start with -conf,
or add its folder to mount_allowed_image_roots in the primary config.
The security page of the manual explains the policy.
```

```
/etc
was refused by the mount policy: system path.
System folders and device names are never mounted, whatever the
configuration says. Copy what the game needs into a folder of its own.
The security page of the manual explains the policy.
```

```
game/link
was refused by the mount policy: a link is in the path.
Mount the real directory the link points to instead.
```

- Log line: `MOUNT: Blocked '<path>' - <reason>`. Reasons: `outside allowed
  directories`, `symlink in path`, `system path`, `path does not resolve`,
  `not a regular file`, `not a recognized disk image`, `not a directory`,
  `reserved device name`.
- Missing path: `game/nothere isn't a directory or valid image file.`
- Allowed roots in force: `mount-policy: allowed base: <path>` lines in the log.

## Mount lock

| Task | curl | dosboxctl |
|---|---|---|
| Lock | `curl -s -H "$A" -X POST $U/mount/lock` | `dosboxctl mount-lock` |
| Status | `curl -s -H "$A" $U/mount/lock` | `dosboxctl mount-lock status` |

- After the lock: MOUNT answers `This operation is not permitted in secure mode.`,
  swap answers 403. `mount -u` still unmounts.
- One way: stays until the engine restarts. Lock after setup when untrusted code
  (a game, a script) runs next.

## Next

- Archives (zip, rar, 7z, iso as a drive): 104.
- Whole install with disk swaps: 202.
