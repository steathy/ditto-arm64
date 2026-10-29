# Initial source import review

Prepared on October 29, 2026, for review before any remote publication.

## Included

- Complete application and bundled-library C/C++ sources from the documented
  upstream snapshot, with the ARM64 port changes.
- Project files, resources, 30 language files and 12 themes.
- ARM64 dependency, build, portable-package, installer and smoke-test scripts.
- A short project README, upstream provenance, original GPLv3 license and existing
  third-party copyright/license notices.
- About-page attribution: `Unofficial ARM64 build by steathy@github`, alongside
  the original Ditto author credits.

## Excluded from this import

- The original working checkout's Git history and machine-specific progress reports.
- Clipboard databases, settings, registry exports, backup folders and test records.
- IDE user files, build caches, compiled objects, executables, libraries and archives.
- Upstream signing bundles and private-key containers, precompiled helper utilities,
  legacy release automation, and other-platform distribution packaging.
- Upstream GitHub workflows that reference its signing organization and release setup.

These exclusions retain the application sources while avoiding inherited credentials
and publication settings. The private remote URL stays in local Git configuration;
it is not embedded in project documentation or distributable metadata.

## Review method

Compared the import against the original tracked-file inventory, checked source and
license preservation, inspected potential credential patterns, and scanned the staged
tree for local usernames, computer names and Windows profile paths. Public upstream
author and library attribution is intentionally retained. The maintainer's requested
public GitHub identity is intentionally included.

`Test-SourceHygiene.ps1` provides repeatable checks and verifies that ignore patterns
exclude generated/private data without hiding ARM64 tools, source, language files or
themes. Source-image assets come from the upstream snapshot. The scan does not claim
to detect every possible secret or inspect local Git configuration and ignored files.

The initial import retains upstream whitespace rather than reformatting all vendor
and application sources. Newly written metadata, documentation and scripts are checked
separately for whitespace errors.

## Verification of this prepared tree

| Check | Result |
| --- | --- |
| Source import inventory | 801 staged files; all selected upstream sources present |
| C/C++ source coverage | All 456 upstream C/C++ source/header files retained |
| Required resources | 30 languages and 12 themes retained |
| Original license | Byte-for-byte match with upstream LICENSE |
| Source hygiene scan | Passed with no findings |
| PowerShell parsing | All eight scripts passed |
| Fresh ARM64 Release build | Passed using MSVC v145 |
| Binary privacy checks | No local user-directory paths or local account name in the three project binaries |
| About attribution | New ARM64 credit and GitHub profile present in the executable |
| Portable package | Seven executable/DLL payloads, including the native update helper |
| Isolated runtime test | Startup, bundled modules, open/close/exit and clean shutdown passed |
| Database and ICU test | Integrity, regex and Unicode conversion passed; zero clips captured |
| Inno Setup compilation | Passed; unsigned snapshot installer produced locally |

The subsequent version/branding refresh sets the community build to 3.25.146.1,
adds the fork repository and manual Releases link to About, and updates installer
support/update URLs. Its Release build, portable smoke test and installer compilation
passed. The updated client was installed locally after a verified backup; all existing
history rows, preferences and startup registration were preserved. The executable's
version, builder credit, both URLs and original author credits were verified.

Community revision 3.25.146.2 adds the original Ditto repository directly beneath
the fork repository in About. The update design and empty stable-channel JSON feed
are included for review; update checking, automatic downloading and installation
are not implemented in this revision.
Its installed version, history/settings preservation, and About links/credits were
subsequently verified, including visual inspection of the About page.

Community revision 3.25.146.3 replaces the Releases hyperlink with a standard
**Check Update** button and removes the unused Apply button before the Options
dialog is laid out, allowing Windows to align OK and Cancel to the right.
The button still opens the fork's Releases page; it does not query the JSON feed.
The Release build, native portable/runtime/database checks and installer compilation
passed. Installation was verified against a fresh backup: existing history rows,
settings and startup registration were preserved, and the installed payload hashes
matched the package. Visual inspection of revision 3 after restart remains pending.

Community revision 3.25.146.4 implements the background JSON check/download workflow,
release notes and progress UI, strict version/URL validation, SHA-256 and publisher
verification, explicit install consent and an independent backup/install/restart
helper. It also updates both repository display labels and adds README affiliation
and modification disclosures. The Release build and 77 isolated updater checks
passed. Trusted publisher keys remain unset: a successful signed automatic upgrade
cannot yet be tested. The live WinHTTP feed request timed out with error 12002.
Packaging verified all seven native payloads and their privacy checks. The isolated
portable/database test and installer compilation passed. The local test installer
was applied after a fresh backup; payload hashes, native execution, existing
history/settings and startup registration were verified. Requested About labels
and controls were also checked in the installed executable. Full visual interaction
with revision 4 and a successful signed automatic upgrade remain outstanding.

Community revision 3.25.146.5 adds the compiled **Nightly Build / Stable Build**
label and a persisted **Include beta updates (Nightly Builds)** preference. Schema 2
uses `updates/arm64.json` with one latest release slot per channel. Stable-only is
the default; beta opt-in chooses the newest eligible channel offer and prefers
stable on ties. Both entries are validated. Verification, cached handoff and the
helper preserve the selected channel; rollback records are separate, and switching
to an older stable offer never downgrades the installed binary.

The expanded 102-check native updater suite, ARM64 Release build, all seven native
payload/privacy checks, isolated runtime/database smoke test and installer build
passed. After a fresh backup, the local installer was applied and installed hashes,
native execution, all existing history/settings and startup registration were
verified. The compiled label and checkbox text were checked in the installed binary.
Full visual interaction with revision 5 remains pending: the Windows automation
tool did not expose a targetable Ditto window. Both feed slots and publisher pins
remain empty; a signed end-to-end automatic upgrade is still untested. No source
or release has been pushed.

Community revision 3.25.146.6 sizes About's status and signing notice using the
actual control font and available width. Empty status takes no space; the initial
Check Update instruction is removed. Repository names now read **Ditto Windows
ARM64** and **Ditto Windows x86/64**, with measured name/address columns aligning
the dashes. The signing notice distinguishes checking/downloading from installation.
The ARM64 Release build, 102 updater checks, portable runtime/database smoke test,
all seven payload/privacy checks and installer compilation passed. Signing policy
is unchanged. After the user saved and closed Options, a fresh backup was verified
and the local installer was applied. Installed payload hashes, native ARM64 execution,
existing history/settings, database path and startup registration all matched the
expected package and backup. Final visual inspection of revision 6 remains pending;
the prior Options window could not be activated by the Windows automation tool.

Community revision 3.25.146.7 restores the upstream information-list height (132
dialog units), keeps repository names and addresses clickable in aligned columns,
and moves signing information from the page body to the Install button tooltip and
installation dialog. Update controls flow below the fixed list.

Unsigned PE releases may now reach an explicit **Install unsigned update / Cancel**
dialog. Both the installer and local app/helper are checked, and the default is
Cancel. Consent is specific to a single handoff; the helper consumes its local
ticket once and enforces consent again when checking unsigned files. There is no
feed or saved-preference switch to approve unsigned installs. Size/hash and
product/version checks remain mandatory. Invalid, untrusted, revoked or damaged
signatures are rejected, even with unsigned consent. An unsigned download does not
advance the channel's signature-verified rollback floor.

The Release build and 120 updater checks passed. The suite exercises real unsigned
PE classification, streamed downloads and cache reuse through production WinTrust
code, actual app/helper preflight, consent gates, and non-replayable handoff records.
No fixture executes an installer. Signed success uses an injected fixture verifier;
a public end-to-end automatic update and real approved signing remain untested.
The revision-7 local installation preserved history, preferences, database path
and startup registration; normal clipboard counters increased while the client ran.
Validation against the actual Inno installer then found that its product-name
resource has trailing-space padding. Revision 3.25.146.8 normalizes only that
padding and resource terminators before comparing the expected product name.
The revised 120-check suite includes padded resources, and a separate read-only
check against the packaged installer confirms that its hash/identity pass and
unsigned consent is both required and accepted. Revision 8 was packaged and installed
after another verified backup. Installed file hashes, native ARM64 execution, existing
history/preferences, database path and startup registration all passed verification.
Final visual inspection remains pending because no targetable Ditto window was open.

Checks were run on October 29, 2026. Test logs and generated packages are kept in
ignored `work/` and `artifacts/` directories. The isolated test used its own database;
the separate installed-client verification used a read-only comparison with its
backup. A clean-machine deployment test remains outstanding.

## Release preparation

Keep GPLv3 for this derived application and retain all component notices. Record the
exact source commit for a release, then run build, package and isolated smoke checks.
Before the first source commit, local test packages explicitly use snapshot provenance.
Signing and publication still require separate setup and approval.
