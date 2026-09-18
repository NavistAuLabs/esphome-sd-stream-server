# Security policy

## Supported versions

Security fixes go onto the most recent tag. There are no long-term support
branches.

| Version | Supported |
| ------- | --------- |
| 0.1.x   | Yes       |
| < 0.1   | No        |

## Report a vulnerability

Report vulnerabilities privately through
[GitHub Security Advisories](https://github.com/NavistAuLabs/esphome-sd-stream-server/security/advisories/new).
Do not open a public issue for a vulnerability.

Include the ESPHome version, the board and framework, a configuration that
shows the problem, and the effect you can demonstrate. Expect an
acknowledgement within seven days.

## Before you report: this serves over plain HTTP

ESPHome's `web_server` has no TLS. Every response this component sends,
including the credentials on a `basic` auth exchange, crosses the network in
the clear. That is a property of the platform, not a defect in this component,
and it is why the component should be reachable only from a network segment
you trust. Reports that amount to "the traffic is unencrypted" will be closed
with a pointer to this paragraph.

## What is in scope

The component reads files from a mounted card and returns them over HTTP, on a
device with no memory protection. That shape defines the interesting attack
surface:

- **Escaping `root_path`.** Any request — a fetch path or a `/file_list`
  `dir` parameter — that resolves to a file outside the configured
  `root_path`. Rejection happens in one place for each endpoint so that it can
  be audited; a way around either is the most serious report this project can
  receive. Note that `curl` normalises `..` client-side, so reproduce with
  `--path-as-is`.
- **Reaching the handler without authentication.** The component has no
  credentials of its own and relies on being registered behind `web_server`. A
  request shape that reaches `handleRequest()` while `web_server`'s auth is
  configured and enabled is in scope.
- **Memory safety in the request path.** A crafted path or filename that
  overflows a buffer, corrupts the heap, or faults the device. A remotely
  triggerable crash counts: this component typically runs on an unattended
  device, and a reboot loses whatever the device was capturing.
- **Resource exhaustion from a single request.** The design holds one
  `chunk_size` buffer and one directory handle at a time, on purpose. An input
  that makes it hold substantially more is a defect.
- **Malformed output from an attacker-controlled name.** Filenames are
  embedded in the `/file_list` JSON. FAT's own character restrictions are what
  currently prevent a name from breaking the encoding — if you can produce a
  name on a supported filesystem that does, that is in scope.

## What is out of scope

- **Serving what you configured it to serve.** Pointing `root_path` at
  something sensitive, or running without `web_server` auth configured, is a
  configuration choice. The component does exactly what it is told.
- **The absence of transport encryption.** See above.
- **The strength of `web_server`'s authentication** — digest versus basic,
  credential handling, session behaviour. That is ESPHome's code; report it to
  ESPHome.
- **Vulnerabilities in ESPHome, ESP-IDF, FATFS, or whatever component mounted
  the card.** Report those to their projects.
- **Physical access to the device or the card.** Anyone holding the card reads
  it directly.
