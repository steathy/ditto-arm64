# GitHub builds and releases

The [Windows ARM64 workflow](../../.github/workflows/arm64-release.yml) builds from
the checked-in source on a fresh GitHub-hosted `windows-11-vs2026-arm` runner with
Visual Studio 2026. It installs SHA-256-pinned, publisher-verified Inno Setup 6.7.4.
The existing build script verifies the pinned zlib revision. Actions are pinned to
commit IDs. No personal machine, clipboard database or signing secret is needed.

## Triggers and outputs

- Pushes to `main`, `nightly` or `codex/**`, pull requests, and manual Actions runs build/test
  and upload downloadable artifacts, retained for 30 days. Diagnostics last 7 days.
- Builds targeting `main` must declare the stable channel and `releaseBranch: main`;
  those targeting `nightly` must declare beta and `releaseBranch: nightly`.
  Pull requests are checked against their destination branch.
- A pushed `vMAJOR.MINOR.BUILD.REVISION` tag additionally publishes a GitHub Release.
  Its commit must already be on the `releaseBranch` declared in
  `build/source-info.json`, and the tag must exactly match both numeric
  and string versions in `src/Arm64BuildInfo.h`. Release notes must exist at
  `docs/releases/MAJOR.MINOR.BUILD.REVISION.md` (at most 8 KiB UTF-8).
- `DITTO_ARM64_NIGHTLY_BUILD=1` publishes a prerelease and updates only `beta`.
  `0` publishes a stable release and updates only `stable`. This flag must reflect
  the tested upstream baseline; tagging a nightly build does not make it stable.
- There is no scheduled nightly run. The current label describes the upstream
  development baseline, not a daily build schedule.

All builds run the native updater checks and isolated portable startup/database/ICU
smoke test before packaging. The payload's seven EXE/DLL files must be ARM64.
The release has exactly three uploaded files: the installer, portable ZIP and one
source ZIP containing both the project and pinned zlib sources. SHA-256 checksums
are displayed in the release notes. GitHub additionally generates its two
repository-only source links. Build manifests, feed metadata, the release body
and `SHA256SUMS` stay under `metadata/` in the Actions artifact.
The installer bootstrapper may use Windows emulation; installed payloads are native.

## Publish a new version

1. Increase both version definitions in `src/Arm64BuildInfo.h`, set the correct
   nightly/stable flag, update the README version and write that version's notes.
2. Commit, review and push the channel's source branch. Verify its Actions run and test the built package
   on Windows ARM64. The full interactive update/install flow remains a manual test.
3. Create and push the matching annotated tag to the `github` remote. For example,
   after increasing the nightly version to a new, unpublished number:

   ```powershell
   git switch nightly
   $version = (& ./tools/arm64/Get-ReleaseMetadata.ps1).version
   git tag -a "v$version" -m "Ditto Windows ARM64 $version beta"
   git push github "v$version"
   ```

4. The workflow builds and tests again, uploads to a draft release, downloads and
   verifies every asset, then publishes it. Published assets are never overwritten.
   It checks the public installer download's size/hash before updating the matching
   channel in `main/updates/arm64.json`. The other channel is preserved, and an older
   release cannot replace a newer offer. Stable releases are marked GitHub Latest;
   beta releases are not.
5. Fetch GitHub and fast-forward local `main` to include the bot's feed commit before
   starting the next change. Push that history to Gitea to keep the review copy current.

The default branch `main` builds stable, based on official release `3.25.113.0` at
`81a667c790f5a6c9b6b63f187eb75e79bf159670`. Stable must contain that upstream
source baseline with the ARM64 port changes; changing only the version flag is
insufficient. The `nightly` branch is pinned to upstream `build-number-146` at
`a80fd3536bc407cac8cce43b5682022282ace983`. The central feed remains on `main`
and receives both channels; clients keep the same feed URL.
An installed 3.25.146.x nightly will not auto-downgrade to a 3.25.113.x stable.

Shared fixes should normally land on `main` and then be merged into `nightly`,
retaining nightly's upstream baseline, version and beta metadata when resolving
conflicts. Do not merge the complete development baseline into `main` merely to
promote a nightly build. Port each new upstream stable release deliberately.

The October 30, 2026 branch migration preserved commit history, published tags,
release assets and both feed entries. Existing version numbers were not republished.
The old `codex/stable-3.25.113` branch remains only as a historical reference for
the original stable tag's workflow metadata; ongoing stable work belongs on `main`.

The release job alone receives `contents: write`; builds and pull requests are
read-only and do not retain Git credentials. GitHub's automatic `GITHUB_TOKEN`
is sufficient. The workflow writes the feed through the Contents API with its
current blob SHA, retrying conflicts without overwriting other `main` changes.
If branch protection later blocks bot writes, feed publication will fail visibly;
adapt that step to a reviewed PR before enabling the restriction.

A failed upload leaves a draft that the publish job can repair. Re-run the failed
publish job with its existing build artifact. A published version with different
assets is rejected: use a new version/tag rather than moving tags or replacing
downloads. Publication can succeed while the feed step fails; rerunning the publish
job verifies the existing assets and finishes the feed update.

## Signing status

These releases are unsigned and require the client's explicit unsigned-install
confirmation. The automation does not claim SignPath approval. Signing requires
a separate application, approved publisher keys, signing of the project binaries
and installer, and changes to packaging/verification before computing final hashes.
Microsoft runtime DLLs retain their existing signatures.

SignPath's GitHub integration requires verifiable hosted builds. Its Foundation
terms also have conditions for upstream-derived projects, visible fork relationships
and release-branch ancestry. The initial import is a documented source snapshot,
not a GitHub-network fork with upstream Git history. Resolve that eligibility point
before applying; this workflow by itself does not establish eligibility.

- [SignPath GitHub integration](https://docs.signpath.io/trusted-build-systems/github)
- [SignPath Foundation conditions](https://signpath.org/terms)
