# Upstream provenance and licensing

This repository contains an unofficial Windows ARM64 port of
[Ditto](https://github.com/sabrogden/Ditto), originally developed by Scott Brogden,
with contributions from Kevin Edwards and the Ditto community.

- Source snapshot: stable tag `3.25.113.0`, commit `81a667c790f5a6c9b6b63f187eb75e79bf159670`.
- Upstream source: https://github.com/sabrogden/Ditto/tree/81a667c790f5a6c9b6b63f187eb75e79bf159670
- ARM64 port and build maintainer: [steathy](https://github.com/steathy).
- This is an independent community build, without an assertion of endorsement by
  the upstream maintainers. Upstream's signing approval does not cover this fork.

The source snapshot preserves application sources, resources, translations,
themes and bundled library sources. Machine-specific IDE files, signing material,
prebuilt utilities, legacy release automation and non-ARM64 distribution packaging
were omitted. ARM64 build and packaging scripts replace the relevant automation.
Upstream Git history was not imported into this initial source snapshot.
This stable branch restores the application and bundled library sources from the
official stable tag, then retains the ARM64 port, About page and fork updater.
See [the stable baseline audit](docs/arm64/STABLE_BASELINE.md).

## License

Retain the upstream GNU General Public License version 3 in [LICENSE](LICENSE).
The ARM64-specific modifications are provided under GPLv3 as part of this work.
The port changes build configuration, dependency integration, packaging, language
fallback and About-page attribution; it does not claim authorship of upstream code.
Keep the original copyright notices and contributor acknowledgements.

Bundled third-party source retains its existing copyright and license notices;
those components are not relicensed by this document. The build retrieves zlib
1.3.2 at commit `da607da739fa6047df13e66a2af6b8bec7c2a498`, under its own zlib license.
Microsoft runtime files are obtained from the installed Visual Studio ARM64
redistributable directory and remain subject to Microsoft's redistribution terms.

When distributing a binary release, provide the corresponding modified source,
the build/packaging scripts and applicable license notices alongside it. Tag the
source used for that release and retain the pinned dependency information.
