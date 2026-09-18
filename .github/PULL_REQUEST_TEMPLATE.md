<!--
Branch from `main` and target `main`. See CONTRIBUTING.md.
-->

## What this changes

<!-- One or two sentences. What behaviour is different after this merge? -->

## Why

<!-- The problem this solves. Link the issue if there is one. -->

Closes #

## How it was run

<!--
Required for anything that changes behaviour; delete this section for a
docs-only change. "Compiles clean" is not evidence — see CONTRIBUTING.md.
-->

- Board and framework:
- ESPHome version:
- What you observed:

## Checklist

- [ ] Run on real hardware, with the result recorded above.
- [ ] `CHANGELOG.md` has an entry under `## [Unreleased]`.
- [ ] Documentation updated if an option, default or endpoint shape changed.
- [ ] Comments that justify a non-obvious choice still match the code.

## Device constraints

<!--
Delete if the change does not touch a request handler. Otherwise confirm both:
the handler adds no recursion and no large stack buffers (the httpd worker
stack is ~4 KB), and nothing is collected into memory before being sent —
files and the listing both stream.
-->
