# Stable source baseline

ARM64 version **3.25.113.1** is based on upstream's official stable tag
[`3.25.113.0`](https://github.com/sabrogden/Ditto/releases/tag/3.25.113.0), commit
`81a667c790f5a6c9b6b63f187eb75e79bf159670`. This was upstream's latest stable
release when checked on October 30, 2026. The final version component identifies
this fork's revision, not a new upstream release.

## Port scope

The source transition from nightly `build-number-146` was applied to every retained
upstream file. Of the 759 retained stable files, 740 matched the stable snapshot
byte-for-byte before branch documentation edits. The remaining differences were
reviewed: build/project configuration, installer, resource/version definitions,
About/update UI, Options layout, English fallback, version formatting, fork README
and ignore rules, plus a line-ending-only difference in `ReadOnlyFlag.h`.
Fork-added updater, build scripts and documentation remain separate additions.

The stable branch removes later clipboard monitoring/paste changes, modern scrollbar
code, theme preview, and nightly-only actions. Its SQLite and encryption library
sources are restored exactly from stable: SQLite **3.45.2**, SQLite3 Multiple Ciphers
**1.8.4**. zlib **1.3.2**, pinned to
`da607da739fa6047df13e66a2af6b8bec7c2a498`, remains an explicit ARM64 dependency
update rather than an assertion that every dependency matches upstream's binary.
ARM64 projects use MSVC v145; other platform project settings retain upstream values.

Upstream application history was not imported into the public fork. Legacy signing
material, generated/prebuilt files and obsolete distribution automation remain
excluded as documented in [UPSTREAM.md](../../UPSTREAM.md). All applicable upstream
and dependency licenses remain in the source bundle.

## Validation and release policy

The same release gates apply to stable and nightly: native compilation, updater
fixtures, seven ARM64 payload checks, isolated startup, SQLite integrity and ICU
checks, installer compilation, source hygiene, and release download/hash verification.
See the tag's Actions run for its actual results; no release is published on a failed gate.
The public source archive bundles pinned zlib for building without a separate zlib
checkout. Full interactive installation and broader clipboard/paste regression tests
remain manual work.

Stable source lives on `codex/stable-3.25.113`; nightly remains on `main`. Both publish
to their own slot in `main/updates/arm64.json`. Stable is marked Latest on GitHub;
nightly is a prerelease. A higher-version nightly installation does not automatically
downgrade to stable. Back up settings/history before a manual switch; isolated tests
do not establish compatibility with every existing nightly database.
