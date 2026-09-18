# sd_stream_server — how it works

Design and implementation reference for the `sd_stream_server` component: a
read-only HTTP file server that **streams** from an already-mounted SD card.
`GET <url_prefix>/<path>` returns the file, and `GET /file_list?dir=<path>`
returns a JSON index of what is there. No upload, no deletion, no MIME
sniffing, no browsable HTML. For installing and using it — device
configuration, the SD mount, troubleshooting — see the root
[README](../../README.md); nothing here is needed to run it.

Auth comes from `web_server`, which this registers behind — there is no
separate credential here on purpose.

## Why this exists instead of `sd_file_server`

`n-serrette/esphome_sd_card` ships an `sd_file_server`. On an Olimex
ESP32-POE-ISO under ESP-IDF it **crashes the board on download** —
`Fault - StoreProhibited (cause 29)`, `EXCVADDR 0x00000004`, reproducible,
immediately after its handler logs "can handle".

It is also a wider component than a read-only pull needs: listing, upload,
deletion and MIME detection are each surface that has to keep working against
a moving ESPHome API, and each is a local patch to carry when it does not.
Upstream is not moving quickly either — as of 2026-09-18 the most recent
commit on that repository's default branch was dated 2026-03-12, with a
`list_directory` crash fix still sitting unmerged. Reimplementing the single
operation actually wanted is cheaper than maintaining patches against the
rest.

The same repository's `sd_mmc_card` is still the right tool for **mounting**,
and this component is built to sit alongside it. That API surface — mount a
VFS path, report free space — barely touches ESPHome core and therefore rots
slowly. The HTTP handler surface is the part that churns.

What is here instead is a single 239-line `.cpp` doing one thing, depending
only on `web_server_base`.

## Why it streams

ESPHome's own `AsyncWebServerResponse` is **buffer-only**:
`AsyncWebServerRequest::send()` calls `httpd_resp_send()` exactly once
with a fully-resident buffer, and `AsyncResponseStream::print()` just
appends to an internal `std::string`. Nothing flushes incrementally. That
is precisely why `sd_file_server`'s `read_file()` reads a whole file into
RAM, and why serving a large file is dangerous on a board with 520 KB of
SRAM and no PSRAM.

True streaming is still possible, just not through that surface:
`AsyncWebServerRequest` exposes `operator httpd_req_t*()`, and from the
raw ESP-IDF handle `httpd_resp_send_chunk()` flushes incrementally. Call
it repeatedly, terminate with a zero-length chunk.

This is not a hack — ESPHome core does the same thing in
`esp32_camera_web_server`, which streams an unbounded MJPEG feed frame by
frame.

Peak RAM is one `chunk_size` buffer regardless of file size. So there is no
serve-size ceiling to measure, no cap on how large a file may be, and no
need to split a large file into parts and reassemble it in the consumer.

## Gotchas

**FATFS long filenames are OFF by default**, capping every path at 8.3 and
making `fopen` fail with `EINVAL` (errno 22) on anything longer. The tell
is a directory listing showing `SPOTLI~1` / `FSEVEN~1`. The fix is the
`CONFIG_FATFS_LFN_HEAP` sdkconfig pair in the root README's example — heap
rather than stack, because the httpd worker task stack is shallow (~4352
bytes) and a 255-byte LFN buffer on it invites an overflow.

**`fatfs` must be re-included in the build.** Since ESPHome 2026.2.0,
built-in ESP-IDF components are excluded by default; the root README's
example carries the `include_builtin_idf_components` / VFS options this
needs.

**Use `url_to()`, not `url()`.** `AsyncWebServerRequest::url()` carries
`ESPDEPRECATED("Use url_to() instead. Removed in 2026.9.0", "2026.3.0")`.
`url_to()` writes into a caller-supplied buffer sized
`AsyncWebServerRequest::URL_BUF_SIZE` and returns a `StringRef`, which
converts to `std::string` implicitly.

**Terminate on success only.** A zero-length chunk ends the response.
Skip it on failure: leaving the response unterminated is what tells the
client the transfer was truncated, rather than handing it a short file
that looks complete.

**Traversal is rejected before touching the filesystem.** Any `..` in the
path is refused. The server addresses a flat set of capture files, so
there is no legitimate use for it and a narrow rule is one that can
actually be audited. Note that `curl` normalises `..` client-side — use
`--path-as-is` to actually test this.

