# Ditto ARM64

An unofficial native Windows ARM64 build of [Ditto](https://github.com/sabrogden/Ditto),
maintained and built by [steathy](https://github.com/steathy) (`steathy@github`).

This community build is specifically for **Windows ARM64**. It is not affiliated
with or endorsed by the original Ditto project or its maintainers.

**Upstream version:** [Ditto 3.25.113.0](https://github.com/sabrogden/Ditto/releases/tag/3.25.113.0)
plus subsequent `master` changes, pinned to commit
[`a80fd35`](https://github.com/sabrogden/Ditto/commit/a80fd3536bc407cac8cce43b5682022282ace983)
(September 30, 2026; upstream tag `build-number-146`).

**Current ARM64 build:** `3.25.146.8` — **Nightly Build**
(upstream nightly build 146, ARM64 revision 8).
Project and future updates: [steathy/ditto-arm64](https://github.com/steathy/ditto-arm64).

Ditto saves clipboard history so you can search and reuse copied text, images,
HTML and other clipboard formats. This port targets Windows 11 on Arm.

Ditto was created by **Scott Brogden**, with contributions from **Kevin Edwards**
and the Ditto community. This independent ARM64 port is not an official upstream
release. See the [original project](https://github.com/sabrogden/Ditto) and its
[wiki](https://github.com/sabrogden/Ditto/wiki) for features, usage and documentation.

- [Build instructions](tools/arm64/README.md)
- [Port status](docs/arm64/STATUS.md)
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

Current local builds are unsigned. Both feed entries are empty until releases are
published. Users can choose to install unsigned updates after a warning; signed
releases must match the fork's approved publisher identity. Signing and public
releases are pending.
Licensed under [GNU GPLv3](LICENSE), following upstream; third-party components
retain their original licenses.
