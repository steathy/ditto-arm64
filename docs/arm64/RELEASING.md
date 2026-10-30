# GitHub builds and releases

The [Windows ARM64 workflow](../../.github/workflows/arm64-release.yml) builds from
the checked-in source on a fresh GitHub-hosted `windows-11-vs2026-arm` runner with
Visual Studio 2026. It installs SHA-256-pinned, publisher-verified Inno Setup 6.7.4.
The existing build script verifies the pinned zlib revision. Actions are pinned to
commit IDs. No personal machine, clipboard database or signing secret is needed.

## Triggers and outputs

- Pushes to `main` or `codex/**`, pull requests, and manual Actions runs build/test
  and upload downloadable artifacts, retained for 30 days. Diagnostics last 7 days.
- A pushed `vMAJOR.MINOR.BUILD.REVISION` tag additionally publishes a GitHub Release.
  Its commit must already be on `main`, and the tag must exactly match both numeric
  and string versions in `src/Arm64BuildInfo.h`. Release notes must exist at
  `docs/releases/MAJOR.MINOR.BUILD.REVISION.md` (at most 8 KiB UTF-8).
- `DITTO_ARM64_NIGHTLY_BUILD=1` publishes a prerelease and updates only `beta`.
  `0` publishes a stable release and updates only `stable`. This flag must reflect
  the tested upstream baseline; tagging a nightly build does not make it stable.
- There is no scheduled nightly run. The current label describes the upstream
  development baseline, not a daily build schedule.

All builds run the native updater checks and isolated portable startup/database/ICU
smoke test before packaging. The payload's seven EXE/DLL files must be ARM64.
The release contains the installer, portable ZIP, corresponding project and zlib
source archives, build manifests, update entry, release notes and `SHA256SUMS`.
The installer bootstrapper may use Windows emulation; installed payloads are native.

## Publish a new version

1. Increase both version definitions in `src/Arm64BuildInfo.h`, set the correct
   nightly/stable flag, update the README version and write that version's notes.
2. Commit, review and push `main`. Verify its Actions run and test the built package
   on Windows ARM64. The full interactive update/install flow remains a manual test.
3. Create and push the matching annotated tag to the `github` remote. For example,
   with an appropriately prepared version `3.25.146.9`:

   ```powershell
   git tag -a v3.25.146.9 -m "Ditto Windows ARM64 3.25.146.9 beta"
   git push github v3.25.146.9
   ```

4. The workflow builds and tests again, uploads to a draft release, downloads and
   verifies every asset, then publishes it. Published assets are never overwritten.
   It checks the public installer download's size/hash before updating the matching
   channel in `main/updates/arm64.json`. The other channel is preserved, and an older
   release cannot replace a newer offer. Stable releases are marked GitHub Latest;
   beta releases are not.
5. Fetch GitHub and fast-forward local `main` to include the bot's feed commit before
   starting the next change. Push that history to Gitea to keep the review copy current.

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
