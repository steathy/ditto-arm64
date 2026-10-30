# Ditto ARM64

An unofficial native Windows ARM64 build of [Ditto](https://github.com/sabrogden/Ditto),
maintained and built by [steathy](https://github.com/steathy) (`steathy@github`).

This community build is specifically for **Windows ARM64**. It is not affiliated
with or endorsed by the original Ditto project or its maintainers.

**Upstream version:** [Ditto 3.25.113.0](https://github.com/sabrogden/Ditto/releases/tag/3.25.113.0),
the official stable release, pinned to commit
[`81a667c`](https://github.com/sabrogden/Ditto/commit/81a667c790f5a6c9b6b63f187eb75e79bf159670).

**This branch's ARM64 build:** `3.25.113.1` — **Stable Build**
(upstream stable 3.25.113.0, ARM64 revision 1).
Project and updates: [steathy/ditto-arm64](https://github.com/steathy/ditto-arm64).

| Download | ARM64 version | Upstream baseline |
|---|---|---|
| [Stable](https://github.com/steathy/ditto-arm64/releases/tag/v3.25.113.1) | 3.25.113.1 | Official stable 3.25.113.0 |
| [Nightly / beta](https://github.com/steathy/ditto-arm64/releases/tag/v3.25.146.9) | 3.25.146.9 | Nightly build-number-146 |

Each release has an installer, portable ZIP and source ZIP including zlib.
Checksums are in the release notes. GitHub also adds repository-only source archives.
Stable source is maintained on [main](https://github.com/steathy/ditto-arm64/tree/main);
[nightly](https://github.com/steathy/ditto-arm64/tree/nightly) tracks upstream development.
Switching from a numerically newer nightly to
stable requires a deliberate manual installation; the updater never downgrades.

Ditto saves clipboard history so you can search and reuse copied text, images,
HTML and other clipboard formats. This port targets Windows 11 on Arm.

Ditto was created by **Scott Brogden**, with contributions from **Kevin Edwards**
and the Ditto community. This independent ARM64 port is not an official upstream
release. See the [original project](https://github.com/sabrogden/Ditto) and its
[wiki](https://github.com/sabrogden/Ditto/wiki) for features, usage and documentation.

- [Build instructions](tools/arm64/README.md)
- [Downloads](https://github.com/steathy/ditto-arm64/releases) and [automated releases](docs/arm64/RELEASING.md)
- [Port status](docs/arm64/STATUS.md)
- [Stable source baseline](docs/arm64/STABLE_BASELINE.md)
- [Update mechanism and release setup](docs/arm64/UPDATER_DESIGN.md)
- [Upstream provenance and credits](UPSTREAM.md)

## Changes in this fork

- Native ARM64 build configuration, dependencies and installer packaging.
- About-page builder credits, links to both projects, and corrected button layout.
- A manual **Check Update** workflow using this fork's JSON feed, with one latest
  stable release and one latest beta release. Stable is the default; **Include beta
  updates (Nightly Builds)** opts into the newest of both. The workflow shows version,
  release notes, download progress, SHA-256/signature verification, and an
  installation prompt. Unsigned releases require an explicit warning/confirmation;
  incorrect hashes and invalid signatures are blocked. Installation uses a separate helper for backup,
  graceful shutdown, Windows elevation and restart.

The port retains Ditto's existing clipboard capture, history, search and paste
features; it does not intentionally redesign their core behavior. Compatibility
fixes and packaging changes are documented in the port status above.

Current builds are unsigned. Tagged builds are tested and published by GitHub
Actions; nightly builds are prereleases in the beta update channel. Users can
choose to install unsigned updates after a warning; signed releases must match
the fork's approved publisher identity. Signing is not yet configured.
Licensed under [GNU GPLv3](LICENSE), following upstream; third-party components
retain their original licenses.
