# Native Windows ARM64 build tools

Build with Visual Studio Build Tools 2026 / MSVC v145, ARM64 MFC/ATL and the Windows SDK. Import `buildtools.vsconfig` into Visual Studio Installer to select the components. Run these commands from the repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\arm64\Build-Arm64.ps1 -Configuration Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\arm64\Build-Arm64.ps1 -Configuration Debug
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\arm64\Package-Portable.ps1
```

`Build-Arm64.ps1` fetches zlib 1.3.2 at verified commit `da607da739fa6047df13e66a2af6b8bec7c2a498`, builds it with CMake/Ninja and the matching dynamic CRT, and builds `CP_Main_10.sln`. Git checkouts need network access for the first restore. Our complete source ZIP instead includes `source-dependencies/zlib` with a file-hash manifest; the script verifies and uses those sources without downloading zlib. GitHub's automatic source archives do not include this external dependency. Dependencies and logs stay under `work`; application outputs are under `ARM64\Release` and `ARM64\Debug`. ARM64 does not require the old x86/x64 NuGet packages.

On an ARM64 laptop the script selects native ARM64 MSBuild and compiler tools. On an x64 host it cross-compiles to ARM64. `-CompilerHost x64` explicitly selects the x64 compiler host; `-Rebuild` requests a clean solution rebuild. A 32-bit MSBuild/compiler host can fail loading this project's MFC precompiled header and is rejected by the main project.

`Directory.Build.targets` removes the local repository prefix from compiled source-file diagnostics and records only the PDB filename in shipped binaries. Packaging rejects binaries containing Windows user-directory paths. PDBs and build logs remain local. The MSVC prefix-mapping option was checked with the supported v145 compiler.

`Package-Portable.ps1` creates a new timestamped folder and ZIP under `artifacts`. It bundles Ditto, its update helper, ICU_Loader, DittoUtil, language/theme data, and required ARM64 runtime DLLs from Visual Studio's redistributable directory. Every shipped EXE/DLL must have an ARM64 PE header. The package is an unsigned development build; signing remains separate work. GitHub Actions runs the build, native tests and packaging on `windows-11-vs2026-arm`; see [automated releases](../../docs/arm64/RELEASING.md).

To test a produced package on Windows ARM64, use the directory path printed by the packaging command:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\arm64\Test-Updater.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\arm64\Test-Portable.ps1 -PackagePath 'artifacts\DittoPortable-arm64-<timestamp>'
```

The portable test requires the current updater probe produced by `Test-Updater.ps1`.
It checks this prerequisite before copying files or launching a process; an explicit
`-UpdaterProbePath` can select the current probe executable.

The test launches a separate copy with its own database, disables clipboard capture, receiving and global hotkeys, checks startup/module loading and open/close/exit commands, and verifies database integrity plus ICU regex/Unicode conversion. It leaves the installed Ditto instance alone. A two-control MFC fixture tests the shared tracking wrapper.
The private-desktop integration test also opens Ditto's actual eight Options pages,
checks initialization, makes representative edits on all six editable pages,
checks About preference isolation, and cancels/reopens the actual sheet.
Actual Options saving is deliberately excluded because upstream General writes
Windows startup registration even in portable mode. Saving/clearing the tracking
state is covered by the two-control fixture. A guard verifies that startup registration
is unchanged; an unexpected write to the test executable is restored and fails the run.
It checks cancellation and acceptance of the production shutdown protocol, requires
the production startup-readiness signal, then enables real capture
in a separate window station with its own clipboard/database. It restores a stored
Unicode clip and pastes into a native edit control. The interactive clipboard is
untouched. Keyboard input delivery, visual inspection and broader application
interoperability remain separate tests. It requires the Release SQLite object produced by the build.

Before committing or publishing, stage the intended source files and run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\arm64\Test-SourceHygiene.ps1
```

This checks for generated/sensitive files, common credential patterns, local account/machine identifiers and user-directory paths. It also verifies ignore rules and the presence of required build sources and assets. It reports filenames and finding categories without printing potential secrets. Review its scope and limitations in the generated `work\source-hygiene.json`; manual review is still needed.

## Updater tests

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\arm64\Test-Updater.ps1
```

This runs 185 native fixture checks without using personal clipboard data or
installing software. Add `-LiveFeed` to test the actual GitHub feed request too.
Signed-ready fixtures use an explicitly injected test verifier; unsigned PE checks
use the production verifier with an unsigned test executable. The shipped client
requires per-install consent for unsigned executables while rejecting invalid signatures.
See the updater guide for signing/publication details.

## Installer

Install [Inno Setup](https://jrsoftware.org/isdl.php) 6.7 or newer, then compile an installer from a verified portable package:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\arm64\Build-Installer.ps1 -PackagePath 'artifacts\DittoPortable-arm64-<timestamp>'
```

Use `-IsccPath` for a compiler outside the detected locations. The compiler used for the first installer was Inno Setup 6.7.4, downloaded from its official GitHub release and verified with a valid Pyrsys B.V. Authenticode signature.

The build script verifies all seven payload PE headers and their package hashes. It writes the installer, SHA-256 manifest and compile log into a new `artifacts\installer-arm64-<timestamp>` directory. The installer bootstrapper can run under Windows emulation; Ditto, the update helper and five bundled DLLs are native ARM64.

`Test-InstallerPolicy.ps1 -IsccPath <path-to-ISCC.exe>` compiles and runs tiny
per-user migration/uninstall fixtures with unique application IDs, temporary
registry keys and synthetic history under `work`. It never installs Ditto or
touches its settings/database. The append negative control reproduces inherited
settings deletion; the production overwrite policy preserves the fixture settings.
It also checks obsolete-file cleanup and all installer-language mappings.
CI runs this test after installing the verified Inno compiler.

The installer uses the existing `Ditto` AppId and 64-bit installation directory. It omits the portable marker, settings files and databases; it does not overwrite existing Ditto preferences. Startup registration retains an existing command. Firewall access is optional. Silent installs do not launch Ditto; start it afterward from the ordinary user session. The installer targets Windows 11 on Arm and is unsigned, clearly marked as a development package. Its version comes from `src/Arm64BuildInfo.h`, with source/snapshot provenance appended to the package name; it is not an official release number. About labels the installed binary **Nightly Build** or **Stable Build** using the same header's build-type flag.

Before replacing an existing installation, quit Ditto cleanly, back up the installation folder, the complete user profile/database folder, `HKCU\Software\Ditto`, startup registration and uninstall metadata. Verify the copied files before installing. Install over the existing directory; do not uninstall first. Preserve the user's startup task selection when supplying installer command-line options. After installation, compare the database and settings with the backup and confirm the running process is native ARM64.

Intentional installer differences from upstream: uninstall logs are overwritten to
discard inherited settings-deletion actions; only the obsolete `vcruntime140_1.dll`
is explicitly removed. Custom themes/languages are not broadly cleaned. Automatic
full crash-dump registration and the Internet Explorer focus default remain omitted.
Windows 11 on Arm remains the supported minimum. Silent installs leave restart to
the normal-user updater helper (or the caller), avoiding competing app launches.
Brazilian Portuguese uses its dedicated translation only when that file is packaged.
About > **Manage update files...** offers normal-user cleanup. Interactive uninstall
offers it only when the helper account matches the desktop shell user; another
administrator's profile is not used as a substitute. Silent uninstall retains cache.

`probes\history_inventory.c` is a read-only verification helper compiled against the Release SQLite object. It reports database integrity and counts without printing clipboard content. With a second database path, it compares every original Main, Data and Types row, including binary clipboard data, and reports missing or changed rows. New rows in the current database are allowed. Close both clients or avoid copying/pasting during comparison, since live clipboard activity can legitimately change rows.

## Isolated dependency probes

These isolated programs check whether Ditto's bundled libraries and Windows dependencies can compile and run as native ARM64. They do not build or certify the full application.

Run from the repository root in a separate PowerShell process:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\arm64\Invoke-DependencyProbes.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\arm64\Invoke-ZlibProbe.ps1
```

Requirements: Visual Studio or Build Tools with ARM64 C++ tools and Windows SDK. The zlib probe also uses Visual Studio's bundled CMake and Ninja. Git/network access is needed to fetch the pinned zlib source if it is absent.

`buildtools.vsconfig` records the ARM64 compiler, MFC/ATL, SDK and CMake component selection for Visual Studio Installer. It selects component families rather than pinning a compiler servicing version.

Both scripts accept `-VisualStudioPath`. The bundled-dependency script also accepts `-SkipChaiScript` and `-Only` with one or more of `windows`, `sqlite`, `tinyxml`, `qrencode`, `chaiscript`.

An additional `-Only mfc` probe checks native ARM64 MFC initialization, string/memory-file operations and ATL. It requires matching ARM64 MFC/ATL components and is only run when explicitly selected:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\arm64\Invoke-DependencyProbes.ps1 -Only mfc
```

Results, executable headers and logs are saved in timestamped directories under `work`. Native execution is skipped on x64 hosts; compilation then checks only the ARM64 target. No existing clipboard database is used. The SQLite test creates its own scratch database.

The dependency script compiles the checked-out SQLite Multiple Ciphers, TinyXML, QR and ChaiScript sources. The Windows test calls ICU and checks system DLL loading without exercising full UI, COM or mail behavior. The zlib script builds upstream 1.3.2 at commit `da607da739fa6047df13e66a2af6b8bec7c2a498` and runs its configured CTest suite on ARM64.

Read `docs\arm64\STATUS.md` for port status and remaining validation work. The standalone dependency probes do not change project settings.
