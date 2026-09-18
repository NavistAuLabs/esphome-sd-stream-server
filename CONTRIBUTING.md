# Contributing

Outside contributions are welcome. This document covers the branch model, the
evidence a change needs to carry, what is in scope for this component, and the
two hardware constraints that shape almost every decision in the code.

## Branch model

- Fork the repository, branch from `main` (`feat/…`, `fix/…`, `docs/…`), and
  open your pull request against `main`.
- Releases are git tags. There are no build artifacts — ESPHome consumes the
  source directly from `github://…@<tag>`.
- **A published tag is never moved or deleted.** People's builds are pinned to
  it, and repointing a tag silently changes what their next compile produces.
  A mistake in a release is fixed by a new tag.

## Your change has to have been run

This is firmware. There is no test suite that can tell you a change is correct,
because the interesting failures are a shallow task stack, a filesystem that
lies about name lengths, and an HTTP surface that changes between ESPHome
releases. Every non-trivial bug in this component's history looked fine on
review and was found only by running it on a board.

So a pull request that changes behaviour should say, in its description:

- The board and framework you ran it on.
- The ESPHome version.
- What you actually observed — status codes, log lines, a checksum comparison,
  the board staying up.

"Compiles clean" is not evidence for this project. A documentation-only change
obviously does not need any of this.

## Scope

The component deliberately has one job: read bytes off an already-mounted card
and get them off the device. Things that are out of scope, and why:

- **Anything that writes** — upload, delete, rename, format. Every write verb
  is a way to lose the data the device exists to capture, and there are other
  components for it.
- **Browsable HTML, directory pages, MIME detection.** The consumer is a
  script, not a browser. `/file_list` returns JSON precisely so nothing has to
  render.
- **Mounting the card.** This component takes a VFS path and nothing more, so
  it depends only on `web_server_base`. Adding a dependency on a mounting
  component would couple the two upgrade cycles together for no benefit.
- **Its own authentication.** It registers behind `web_server`, so
  `web_server`'s auth applies. A second credential here would be a second
  thing to get wrong.

New configuration options are not free — each one is documented, supported,
and kept working indefinitely. Bring a concrete case the current schema cannot
express; "might be useful" is not sufficient on its own. If you are unsure,
open an issue before writing the PR.

## Two constraints that are not style preferences

**The ESP-IDF httpd worker task runs on a shallow stack** — roughly 4 KB, much
of which the FATFS and SDMMC call chain has already consumed before any of this
code runs. Listing the card root with a recursive walk crashed a board
outright; the same walk, iterative over a heap worklist, does not. Do not add
recursion, large stack buffers, or per-level allocations to anything that runs
in a request handler.

**Nothing is allowed to become fully resident.** A file is streamed in
`chunk_size` pieces and the JSON listing is emitted as the walk proceeds,
because peak RAM has to stay flat as the card fills up. A change that collects
into a `std::string` or a `std::vector` before sending defeats the entire
reason the component exists.

Both of these are why the code drops to the raw `httpd_req_t *` under
ESPHome's response object. That is not a hack — ESPHome core does the same in
`esp32_camera_web_server` — but it does mean the ESP-IDF rules apply directly,
including terminating a response with a zero-length chunk only on success.

## Style

Match the surrounding code. It follows ESPHome core's conventions: 2-space
indent, a 120-column limit, `snake_case_` trailing-underscore members, and
`ESP_LOGx` with the file's `TAG`.

Comments in this component explain *why*, especially where the obvious
implementation is the wrong one. If you change something a comment justifies,
update the comment in the same commit.

## Changelog

This project follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Add your entry to [CHANGELOG.md](CHANGELOG.md) under `## [Unreleased]` in the
same pull request that makes the change, not at release time. Anything that
changes a default, an endpoint's shape, or what `/file_list` includes gets an
entry regardless of how small the diff is — those are the changes that break a
consumer silently.

## Code of conduct

This project follows the [Contributor Covenant](CODE_OF_CONDUCT.md). Reports go
to `foss+conduct@navist.com.au`.
