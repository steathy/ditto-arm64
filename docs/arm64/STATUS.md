# ARM64 port status

This is an unsigned community port for Windows 11 on Arm, maintained by
[steathy](https://github.com/steathy). It is not an official Ditto release.

## Source and release channels

| Channel | ARM64 version | Upstream source | Branch |
|---|---|---|---|
| Stable | 3.25.113.1 | Official stable 3.25.113.0, `81a667c` | `main` |
| Beta / nightly | 3.25.146.9 | `build-number-146`, `a80fd35` | `nightly` |

This branch builds the **nightly** version. The stable branch restores the upstream
stable application and bundled library sources while retaining the ARM64 port and
fork updater. Version, channel and upstream provenance are declared in
`src/Arm64BuildInfo.h` and `build/source-info.json`.
The initial local test displayed 3.24.238.2 because upstream's checked-in version
resource was stale; the port now supplies its own consistent binary/installer version.

## ARM64 and fork changes

- MSVC v145 ARM64 build configuration and corrected output, library and header paths.
- Pinned zlib 1.3.2 built from source; source releases also bundle that dependency.
- Native Ditto, update helper, ICU_Loader, DittoUtil and three Microsoft runtime DLLs.
- An Inno Setup installer with settings/history preservation and backup support.
- Corrected English language fallback and ARM64/version identification.
- About-page builder credits and aligned links to both repositories, original-size
  information list, compact update controls, and right-aligned OK/Cancel buttons.
- Compiler path trimming and package checks for personal directory disclosures.

The port retains the chosen upstream baseline's clipboard, search and paste behavior.
See [upstream provenance](../../UPSTREAM.md) for credits and licensing.

## Updates and publication

About's **Check Update** reads this fork's JSON feed on request, shows release notes
and progress, downloads compatible newer installers, and verifies size, SHA-256,
product/version metadata and signature status. Stable is the default; **Include beta
updates (Nightly Builds)** opts into the newest offer across both channels. The
updater never downgrades: switching from 3.25.146.x nightly to 3.25.113.x stable
requires a deliberate manual installation. Back up settings/history before switching
to an older upstream baseline; compatibility with an existing nightly database is
not established by the isolated fresh-database test.

Unsigned updates require explicit **Install unsigned update / Cancel** consent,
defaulting to Cancel, before Windows UAC. Signed updates require valid Authenticode
trust and an approved publisher key. Signing and approved keys are not configured.
The helper coordinates backup, graceful shutdown, installation and restart.

GitHub Actions builds, tests and publishes tagged releases, with exactly three
uploaded files: installer, portable ZIP and complete source ZIP. Internal build
metadata stays in Actions artifacts; checksums appear in the release notes.
GitHub adds two automatic repository source archives. The central
`main/updates/arm64.json` contains one latest entry per channel.
See [release automation](RELEASING.md) and [updater design](UPDATER_DESIGN.md).

## Verification and remaining work

Each release workflow runs native updater fixtures, ARM64 payload checks, isolated
portable startup, SQLite integrity and ICU regex/Unicode checks, installer compilation,
source hygiene checks and downloaded release-asset verification. Per-machine logs,
registry exports, databases and history backups remain outside Git.

A prior local nightly installation verified preservation of the user's preferences
and history. A full interactive upgrade through the public feed, stable-over-nightly
database compatibility, clean-machine install/uninstall, broader clipboard/paste
regression testing and performance measurements still need manual validation.
Release checks do not substitute for those tests. Signing remains separate work.

Only source, documentation and build scripts belong in Git. Generated binaries are
release assets. Do not reuse upstream signing credentials or imply signing approval.
