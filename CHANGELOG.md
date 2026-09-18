# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Anything that changes a default, the shape of an endpoint's response, or what
`/file_list` includes is recorded here regardless of size — those are the
changes that break a consumer without breaking a build.

## [Unreleased]

## [0.1.0] - 2026-09-18

First public release.

### Added

- `sd_stream_server`: an ESPHome external component that serves files from an
  already-mounted SD card over HTTP, read-only.
- `GET /<url_prefix>/<path>` streams a file back in `chunk_size` pieces, so
  peak RAM is one buffer regardless of file size. Responses are left
  unterminated on a read or send failure, so a client sees a truncated
  transfer rather than a short file.
- `GET /file_list?dir=<path>` returns a recursive JSON index of the card,
  streamed as the walk proceeds. Dot-prefixed entries are skipped, and the
  walk is iterative so its stack depth does not grow with directory nesting.
- Configuration for `url_prefix`, `root_path` and `chunk_size`; `root_path`
  defaults to `/sdcard`, where `sd_mmc_card` mounts.
- Authentication is inherited from `web_server` rather than reimplemented —
  the component registers as a `web_server_base` handler.
- Path traversal is rejected before the filesystem is touched, on both
  endpoints.
