# ARM64 port status

This is an unsigned development port for Windows 11 on Arm, maintained by
[steathy](https://github.com/steathy). It is not an official Ditto release.

## Version and updates

The current community build version is **3.25.146.8 — Nightly Build**: upstream CI build tag
`build-number-146`, followed by ARM64 port revision 8. It is declared in
`src/Arm64BuildInfo.h` and used by the executable resource and installer.
The upstream source snapshot is still `a80fd35`, after official release 3.25.113.0.
The initial local test build displayed 3.24.238.2 because the upstream checked-in
resource had that old value; upstream CI normally replaces it during packaging.

About's **Check Update** button now checks this fork's stable/beta JSON feed in the
background, displays release notes/progress, downloads newer compatible installers,
and verifies size, SHA-256, product/version metadata and signature status.
Signed files require valid Authenticode trust and an approved publisher identity.
Unsigned updates require **Install unsigned update / Cancel**, with Cancel as the
default, before Windows UAC. A native
helper coordinates backup, shutdown, installation and restart. Checks occur only
on a button click; the old upstream `CheckForUpdates` setting remains unused.
The feed contains exactly one latest release slot per channel. Stable-only is the
default; **Include beta updates (Nightly Builds)** selects the newer offer across
both channels, preferring stable on equal versions. Channel preferences are saved
immediately. Switching preferences clears any pending installation offer, requires
a fresh check, and never downgrades an installed build. The installed build's label
comes from its compiled build type, independently of update preferences.

About links to the fork repository and the original repository directly beneath it,
displays `steathy@github` as the ARM64 builder,
and preserves the original author credits. Installer publisher, support and update
links also point to the fork. General usage/help links still lead to upstream's wiki.
Revision 7 restores the information list's original 132-dialog-unit height. Both
repository names and addresses are clickable, aligned in measured columns. Update
messages flow below the list, with no empty initial instruction/status row. Signing
information is in the Install button tooltip and confirmation dialog, not the page body.
Revision 8 also accepts Inno Setup's trailing-space padding in product-name resources
while preserving the exact expected product identity after that padding is removed.

The [updater guide](UPDATER_DESIGN.md) describes the implementation and release setup.
Both feed entries are empty and the compiled publisher-key list is intentionally empty.
Unsigned local releases can be installed after explicit per-install consent;
unsigned consent does not override invalid signatures or hash/identity mismatches.
Publishing a real feed/release and testing an automatic upgrade through the live
endpoint are still outstanding. The live feed request previously timed out on this
laptop. The expanded updater fixtures cover unsigned PE downloads and consent gates.
The Options dialog omits the unused Apply button before layout, keeping OK and
Cancel aligned to the right without an empty button slot.

## Changes

- Use MSVC v145 for ARM64 projects and a 64-bit compiler/MSBuild host.
- Correct ARM64 output paths, precompiled-header settings and static-library target.
- Build pinned zlib 1.3.2 from source and use its matching headers and library.
- Package Ditto, its update helper, ICU_Loader, DittoUtil and three ARM64 runtime DLLs.
- Produce an Inno Setup installer that preserves existing user preferences and
  databases during an in-place installation.
- Correct the first-run English language fallback and identify ARM64 builds in About.
- Remove developer-specific directory paths from compiled file diagnostics and PE
  debug records, and reject packages that retain Windows user-directory paths.

## Verification

Release and Debug builds, native execution, bundled module loading, isolated
database creation, SQLite integrity, and ICU regex/Unicode operations were tested
during port development. A prior local installation also verified preservation of
existing preferences and history. Per-machine logs, paths, registry exports,
database backups and history counts are intentionally excluded from this repository.

See [the build guide](../../tools/arm64/README.md) for repeatable commands. Further
clipboard/paste, preview, networking, clean-machine install/uninstall, and performance
testing is still needed. Signing and automated publication are not configured.

The cleaned source snapshot was rebuilt and packaged on October 29, 2026. Its source
hygiene check, binary path checks, installer compilation and isolated runtime/database
tests passed. See [the repository review](REPOSITORY_REVIEW.md) for scope and results.

## Publication boundaries

The application and installer are development builds and are unsigned. Do not reuse
upstream signing credentials or claim SignPath approval before this fork is accepted.
Only source and build instructions belong in Git. Generated binaries belong in
release assets, and private settings/history/backups remain local.
