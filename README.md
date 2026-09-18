# esphome-sd-stream-server

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

An ESPHome external component that serves files from an SD card over HTTP,
read-only, by streaming them.

Streaming is the point. ESPHome's own file serving holds the whole file in
RAM, which on a classic ESP32 (520 KB, no PSRAM) puts a ceiling on file size
that a recording or a day of samples will eventually hit. This component
streams instead: peak RAM is one `chunk_size` buffer (4 KiB by default) no
matter how large the file is.

It has no write surface at all: no upload, no delete, no rename, no MIME
sniffing, no browsable HTML. Two endpoints — fetch one file, or list what is
on the card as JSON — which is what a scheduled puller on the other end
actually needs, and nothing else to keep working as ESPHome's HTTP API moves.

Authentication is not reimplemented here. The component registers as a handler
on `web_server_base`, so whatever `web_server` is configured to require
applies before any of this code runs.

## Installation

### Requirements

- **The ESP-IDF framework.** The implementation is wrapped in
  `#ifdef USE_ESP_IDF`. On the Arduino framework it compiles to nothing, the
  handler is never registered, and every request 404s from ESPHome's own
  router — a silent no-op, not a build error.
- **ESPHome 2026.3.0 or later.** Older versions fail the build.
- **The card already mounted.** This component does not touch SD hardware — it
  reads a VFS path. Mount the card with something else first; `sd_mmc_card` is
  the usual choice and mounts at `/sdcard`.
- **`web_server`** configured, for authentication and for the HTTP server
  itself. `web_server_base` is auto-loaded, but on its own it has no
  credentials.
- **FATFS long filename support** if any file on the card has a name longer
  than 8.3. It is off by default and the failure is not obvious — see
  Troubleshooting.

### 1. Add the component to your device configuration

```yaml
external_components:
  - source: github://NavistAuLabs/esphome-sd-stream-server@v0.1.0
    components: [sd_stream_server]
  # Mounts the card this serves from. Pinned at the commit upstream's v0.2.0
  # tag points at: a branch moves, and a tag can be repointed.
  - source: github://n-serrette/esphome_sd_card@889073e052275aeb9e826b697efe3bbbe09f935d
    components: [sd_mmc_card]

esp32:
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_FATFS_LFN_HEAP: "y"
      CONFIG_FATFS_MAX_LFN: "255"
    advanced:
      include_builtin_idf_components: [fatfs]
      disable_vfs_support_termios: false
      disable_vfs_support_select: false
      disable_vfs_support_dir: false

web_server:
  version: 2
  auth:
    username: !secret web_username
    password: !secret web_password
    type: digest

sd_mmc_card:
  id: sd_card
  # Pins are board-specific -- these are an Olimex ESP32-POE-ISO wired for
  # 1-bit mode. Use your own board's SD pinout.
  mode_1bit: true
  clk_pin: GPIO14
  cmd_pin: GPIO15
  data0_pin: GPIO2
  format_if_mount_failed: false

sd_stream_server:
  id: file_server
```

### 2. Confirm it serves

```sh
curl -u user:pass http://<device>/file_list
curl -u user:pass -o out.bin http://<device>/file/somefile.bin
```

The boot log's `dump_config()` line reports the address, the resolved prefix,
`root_path` and `chunk_size` — check it against where your card actually
mounted if either call 404s.

## Configuration

| Option | Type | Default | Notes |
| --- | --- | --- | --- |
| `id` | ID | generated | Only needed if something else refers to the instance. |
| `url_prefix` | string | `file` | Path segment files are served under. A leading `/` is added if absent and trailing `/` are stripped, so `file`, `/file` and `/file/` are equivalent. |
| `root_path` | string | `/sdcard` | Where the card is already mounted. Every served path is resolved under this and cannot escape it. |
| `chunk_size` | int, 512–32768 | `4096` | Bytes read from the card and flushed per chunk. This is peak RAM for a transfer, regardless of file size. |
| `web_server_base_id` | ID | generated | The `web_server_base` instance to register on. Set it only if you have more than one. |

`web_server_base` is auto-loaded.

## Endpoints

### `GET /<url_prefix>/<path>` — fetch a file

Streams `<root_path>/<path>` back as `application/octet-stream` with
`Cache-Control: no-store`, in `chunk_size` pieces.

A `404` with body `{"error":"not found"}` is returned when the file does not
open, when the path contains `..` anywhere, and for the bare prefix with no
path after it. There is deliberately no listing at this endpoint. Any query
string is discarded before the path is resolved.

If a read or a send fails partway through, the response is left
**unterminated** on purpose. An HTTP client sees a truncated transfer and can
retry, rather than receiving a short file that looks complete.

### `GET /file_list?dir=<path>` — list the card

Returns `{"files":[{"path":"/sub/name.ext","size":1234}]}` as
`application/json`, walking recursively from `<root_path>/<dir>`. Each `path`
is relative to `root_path`. Omit `dir` to list the whole card. The array is
streamed as the walk proceeds, so it is never assembled in RAM.

This endpoint's path is fixed at `/file_list`. It is not placed under
`url_prefix`, so do not map another handler there.

Four things are skipped rather than listed, all of them deliberate:

- **Directories** themselves — only files get an entry.
- **Anything whose name starts with `.`** — which covers in-progress writes
  under a dotted directory, and the `.Spotlight-V100` / `.fseventsd` trees a
  card picks up from being formatted on a Mac.
- **Anything more than six path separators deep**, a guard against a
  filesystem loop rather than a limit on real nesting.
- **Entries whose JSON object would not fit a 320-byte buffer** — in practice,
  a pathological path length.

A `dir` containing `..` returns `404`.

## Troubleshooting

**Every path over eight characters fails, and `errno` is 22 (`EINVAL`).**
FATFS long filename support is compiled out by default, capping paths at 8.3.
The tell is a listing full of names like `SPOTLI~1` and `FSEVEN~1`. Set
`CONFIG_FATFS_LFN_HEAP: "y"` and `CONFIG_FATFS_MAX_LFN: "255"` in
`sdkconfig_options` — the heap variant, not the stack one.

**The build cannot find `fatfs`, `opendir`, or `dirent.h`.** Since ESPHome
2026.2.0 the built-in ESP-IDF components are excluded from the build by
default. Re-include it, and re-enable the VFS features the walk needs:

```yaml
esp32:
  framework:
    advanced:
      include_builtin_idf_components: [fatfs]
      disable_vfs_support_termios: false
      disable_vfs_support_select: false
      disable_vfs_support_dir: false
```

**Everything returns 404, including files you can see on the card.** Check the
log line the component prints at boot — `dump_config()` reports the address,
the resolved prefix, `root_path` and `chunk_size`. A `root_path` that does not
match where the card actually mounted is the usual cause. A failed `fopen` is
logged with its path and `errno`: `ENOENT` (2) means the file genuinely is not
there, `EINVAL` (22) means the name broke a FATFS limit.

**Everything returns 401.** That is `web_server`'s authentication, applied
before this component is reached. It is not separately configurable here.

**Path traversal appears to be allowed — or appears to be blocked when you
have not enabled anything.** `curl` normalises `..` client-side before the
request is sent, so neither result means what it looks like. Test with
`curl --path-as-is`.

**`/file_list` is empty or missing entries.** `opendir` failures are logged
individually with the directory and `errno`, so check the log before assuming
the card is empty. If entries appear only for shallow directories, look at
whether the mount limits concurrent open handles — the walk holds one
directory handle at a time for exactly that reason, but a low limit elsewhere
in the configuration can still bite.

**The component is present in the config but nothing is served.** Confirm the
framework is `esp-idf`. On Arduino the whole implementation is preprocessed
away without a warning.

## Contributing

Outside contributions are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for
the branch model and what a change needs to show. Participation is governed by
the [Code of Conduct](CODE_OF_CONDUCT.md). To report a vulnerability, follow
[SECURITY.md](SECURITY.md) rather than opening an issue. Released changes are
recorded in [CHANGELOG.md](CHANGELOG.md).

The component's own implementation notes — why it streams, what the ESP-IDF
escape hatch is, and the traps the platform sets — live next to the code
in [components/sd_stream_server/README.md](components/sd_stream_server/README.md).

## License

MIT. Copyright (c) 2026 Joshua Hogendorn. See [LICENSE](LICENSE).
