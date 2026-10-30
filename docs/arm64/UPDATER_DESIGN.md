# ARM64 update mechanism

Implemented in revision **3.25.146.4**, with stable/beta support in **3.25.146.5**.
Signing is still pending. The [GitHub release workflow](RELEASING.md) builds/tests
version tags, publishes verified unsigned assets and updates the matching channel.
The local build/packaging scripts do not publish anything.

## User experience

About displays the clickable project names **Ditto Windows ARM64** and
**Ditto Windows x86/64**, each linked to its GitHub repository. Duplicate URL text
has been removed. The original 132-dialog-unit information list height stays fixed.
Only the controls below it flow with their content, and
the initial status is empty and takes no space. **Check Update** fetches this fork's
JSON feed without opening a browser. Starting Ditto or opening About makes no request.

Signing is not needed to check or download updates. Revision 7 also allows an
unsigned executable to proceed after an explicit **Install unsigned update / Cancel**
warning, with Cancel as the default. The page has no permanent signing notice;
the Install button tooltip explains the warning. The dialog identifies an unsigned
installer or unsigned local client/helper and explains that a matching hash alone
does not identify its publisher. Consent is specific to that attempt and is never
remembered as a preference. SignPath is one possible signing provider, not a
dependency of the update mechanism. Signed releases still require a valid signature
and the approved publisher key compiled into the client. Those keys remain unset.

The installed version is labeled **Nightly Build** or **Stable Build**, controlled
by `DITTO_ARM64_NIGHTLY_BUILD` in `Arm64BuildInfo.h`; it is not relabeled when the
update preference changes. The default is stable-only, including on a nightly
client. **Include beta updates (Nightly Builds)** opts into the newest of the two
offers; equal versions prefer stable. A newer stable release is also offered to
beta users. A beta-only feed never supplies an update to stable-only users.

The checkbox persists immediately as `Arm64IncludeBetaUpdates` through Ditto's
normal settings API, independently of the Options dialog's OK/Cancel buttons.
It is disabled during checks, downloads, verification and installation. Changing it
clears the current offer and requires another manual check; cached files are not
automatically installed. An older offer is displayed without downloading or
downgrading. Same-version channel promotion does not reinstall/relabel the binary;
publish a higher community revision when a new stable binary is required.

The background worker reports the available version, plain-text release notes,
a link to full notes, download progress and verification status. Newer compatible
installers download automatically. Equal/older releases do not download. Cancel or
closing About cancels unfinished work and removes its partial file. A blocked
network operation can take up to its timeout to observe cancellation.

After verification, the installation dialog asks for permission, with Cancel as the
default. The cached download is reverified after a fresh check, including after
restart. Consent is never saved for later installations. Modified Options pages and
the built-in clip editor block installation until saved/closed. The prompt also
asks the user to save/close external editors; their unsaved state is not observable.

The independent helper verifies the installer again, closes Ditto gracefully,
backs up data, invokes Windows UAC, waits for Setup and restarts Ditto as the original
unelevated user. UAC cancellation or backup/install failure restarts the previous
client where possible. Setup displays progress and never reboots automatically.
After setup succeeds, the installed version is verified and Ditto is restarted;
the helper then displays **Update successful**, including the installed version.
If setup returns 3010, that same dialog includes the Windows restart notice.
Cancellation, failed setup, verification failure or a failed restart does not show
success. Updates initiated by an older client still use its older helper; the new
completion dialog applies to updates initiated from these revised builds.

The helper starts with visible window defaults and suppresses ordinary Shell
launch-error dialogs so errors return to its recovery code. UAC is still shown by
Windows. A private `install.log` in the update handoff directory records stages,
file paths and Windows error numbers; it does not contain clipboard contents or
exported settings. Failure dialogs include its location.

## Feed format

Compiled endpoint:
`https://raw.githubusercontent.com/steathy/ditto-arm64/main/updates/arm64.json`

The checked-in [feed](../../updates/arm64.json) starts with both channel entries
set to `null`. The release workflow fills a channel after verifying its public
installer. Each key accepts exactly one release object, not a list. Replace that channel's entry when
publishing its newest release; keep the other channel's entry. Old release assets
may remain available for recovery. HTTP errors and timeouts are
reported as errors, never as "up to date".

Complete illustrative future feed (replace the placeholder hash and size):

```json
{
  "schemaVersion": 2,
  "appId": "steathy.ditto-arm64",
  "platform": "windows",
  "architecture": "arm64",
  "channels": {
    "stable": null,
    "beta": {
      "version": "3.25.146.9",
      "publishedAt": "2026-10-29T12:00:00Z",
      "minimumWindowsBuild": 22000,
      "releaseNotesUrl": "https://github.com/steathy/ditto-arm64/releases/tag/v3.25.146.9",
      "releaseNotes": "Describe changes here.\nNotes are displayed as plain text.",
      "installer": {
        "url": "https://github.com/steathy/ditto-arm64/releases/download/v3.25.146.9/DittoSetup_3.25.146.9_arm64.exe",
        "sha256": "REPLACE_WITH_SHA256_OF_FINAL_SIGNED_INSTALLER",
        "size": 12345678
      }
    }
  }
}
```

`releaseNotes` is optional, limited to 8 KiB UTF-8. The strict parser rejects
unknown/duplicate keys, incorrect types, invalid Unicode, excessive nesting and
integer overflow. Versions have four canonical numeric components, each 0-65535;
comparison is numeric, so `.10` is newer than `.2`.
Both channel entries are validated, even when beta is disabled. The channel key
determines the displayed build label. Schema 1's unpublished stable-only endpoint
is retired; older local clients need a manual installation of revision 5 or newer.

## Transport and trust

- WinHTTP uses normal HTTPS validation, system proxy discovery, bounded requests
  and explicit redirect validation. No token, clipboard content, telemetry or
  custom authentication credential is sent.
- Checks fetch the feed with cache-revalidation headers. The feed limit is 64 KiB;
  the installer limit is 200 MiB with an exact declared byte count. Network stages
  have 10/15-second timeouts, and read loops have total request deadlines.
- Initial downloads must be from this exact repository's GitHub release asset path.
  Redirects may use that path or the explicit host
  `release-assets.githubusercontent.com`. HTTPS is required throughout; alternate
  ports, user information, traversal and encoded path separators are rejected.
- Downloads use a private per-user Local AppData cache, separate from the installation
  and clipboard database. Reparse points are rejected; file operations support long
  paths. Only the updater's own unique partial files are removed on failure/cancel.
- Installation always requires matching size/SHA-256 and product/file-version
  metadata. Signed files additionally require Windows Authenticode trust/revocation
  verification and an approved signer key. Only `TRUST_E_NOSIGNATURE` with a valid
  PE header and an empty certificate-table directory counts as unsigned; malformed
  executables, damaged/untrusted/revoked signatures and wrong publishers stay blocked,
  even if unsigned installation was approved. A hash from the same repository is not independent
  proof of authenticity if that repository is compromised.
- Approved keys are SHA-256 hashes of DER SubjectPublicKeyInfo in
  [TrustedPublishers.h](../../src/update/TrustedPublishers.h). **The list is empty
  until this fork receives its signing identity.** The feed/registry cannot disable
  checks or grant consent. Unsigned files require the local confirmation dialog.
- Before launch, the app and helper independently reverify the file. The helper
  holds read locks denying writes/deletion, including ancestor-directory locks,
  through elevation and Setup exit, preventing a verified path from being replaced.
- The installer bootstrapper may be emulated. Packaging verifies all seven installed
  EXE/DLL payloads as ARM64; the bootstrapper's PE architecture is not used for this.
- The cache records the highest signature-verified offer separately for stable and beta and
  rejects older offers within that channel. The original `highest-version` record
  remains the stable record; beta uses `highest-version-beta`. A downloaded beta
  does not block a stable offer newer than the installed version. Merely downloading
  an unsigned offer does not raise this floor. The installed version always prevents
  a downgrade, whether signed or unsigned. This does
  not detect withheld releases; signed metadata with expiry is a future enhancement.

## Installation, backup and recovery

The updater supports registered installations with registry settings, a local database
and an unelevated Ditto session. Portable, Store/package-manager, INI, unregistered,
elevated and network-database configurations may check but do not automatically
download/run the system installer.

A checked copy of the installed helper runs independently from the private cache.
The worker checks the current app/helper as well as the installer so the dialog
can warn if any of them is unsigned. The schema-3 local handoff carries the current
choice; the helper accepts tickets only under its private install directory and
consumes each ticket exactly once. It rechecks the files and requires that explicit
choice for unsigned files, including when checking the newly installed client.
It confirms the parent process, executable and registered installation match. After
confirming the handoff channel matches the selected release in the cached feed and
clean shutdown, it locks the actual database plus WAL/SHM sidecars to exclude active
writers, copies and compares their bytes, backs up application files, exports
`HKCU/Software/Ditto`, and records startup registration and original paths. Failed
backup or insufficient space prevents installation. Backups stay under the local
`DittoARM64-Updates/backup-*` folder and must never be committed or published.

The helper passes fixed installer switches, the existing installation directory and
startup-task selection. JSON cannot supply commands, arguments or trust anchors.
The installer runs in place with `/NORESTART`; Ditto is never uninstalled first.
After Setup, the helper checks the installed app's version, product and signature
status before restarting, using only the consent from this installation attempt.

Recovery is manual: never automatically overwrite newly captured clips with an old
database. Error messages include the backup location. A failed installer can leave
partially updated application files; retain the backup for recovery.

## Signing and release preparation

1. Obtain this fork's approved signing identity and add its public-key hash to the
   compiled publisher policy. Confirm the provider's key lifecycle. For rotation,
   first ship a client trusting both the old and new key.
2. Build/test ARM64, then sign Ditto, its helper and project DLLs. Retain Microsoft's
   runtime signatures. The helper's signed product name is `Ditto ARM64 Update Helper`;
   the app and installer use `Ditto ARM64`.
3. Package the signed files, compile/sign the installer and verify it. Current scripts
   label development snapshots; production provenance/signing still need setup.
4. Compute hash/size after signing. Upload immutable versioned assets and notes,
   verify the public download, then replace only the appropriate channel entry in
   the feed last. For stable releases set `DITTO_ARM64_NIGHTLY_BUILD` to 0, increment
   the community revision, rebuild and validate; beta releases use 1. The first
   stable port should use an upstream stable baseline and complete ARM64 testing.
5. Exercise a real signed update on a disposable ARM64 installation before stable
   publication: UAC cancellation, backup failure, local custom DB paths, WAL,
   history/settings preservation, installation failure and restart.

Nothing authorizes a Git push or publication without the maintainer's approval.
Unsigned releases can be offered in either channel, with the same mandatory hash
checks and per-install warning. The maintainer confirmed a successful unsigned update from 3.25.146.8 to
3.25.146.9 on October 30, 2026. This does not validate signed updates or every failure path.

## Verification and limits

`tools/arm64/Test-Updater.ps1` runs 122 checks against the same parser, workflow,
hash, file-locking and Windows verification code. Coverage includes malformed feeds
and URLs, versions, empty/equal/older offers, unsupported modes, cancellation,
partial/oversized/corrupt downloads, cache tampering/reuse, rollback offers,
unsigned PE classification/consent, damaged signature rejection, single-use handoffs,
and file/ancestor replacement locks. Missing-file and missing-directory launch
tests reproduce the old hidden-window startup and require an error within a
bounded timeout, preventing a regression of the invisible Shell dialog hang. Ready-state fixtures
also cover stable/beta selection, equal-version preference, required channel slots,
opt-in/default behavior, beta verification/cache reuse, independent rollback records,
and switching from an installed beta to an older stable without downgrading. Fixtures
for signed releases inject a hash-only verifier inside the test binary. Unsigned PE
download/cache and local app/helper checks use the production Windows verifier.
No fixture executes an installer;
these are not successful production signature tests.

`-LiveFeed` exercises the actual WinHTTP endpoint. On October 29, 2026 it timed out
with Windows error 12002 on the previous stable-only endpoint; the expanded local
suite passed. The release workflow checks public asset availability before updating
the new feed. The maintainer subsequently confirmed the unsigned .8-to-.9 upgrade;
the revised success dialog still requires a complete update initiated from a new
client to validate it end to end. Signed publication requires signing setup. UI, package
and local test-installation results are recorded in the repository review.

## References

- [GitHub release assets](https://docs.github.com/en/rest/releases/assets)
- [Windows trust verification](https://learn.microsoft.com/en-us/windows/win32/api/wintrust/ns-wintrust-wintrust_data)
- [WinHTTP proxy support](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpopen)
- [Inno Setup switches](https://jrsoftware.org/ishelp/topic_setupcmdline.htm)
