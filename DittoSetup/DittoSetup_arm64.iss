; Build with tools/arm64/Build-Installer.ps1. The payload is validated there.
#ifndef PackagePath
  #error PackagePath must name a verified ARM64 portable package
#endif
#ifndef BuildLabel
  #define BuildLabel "local"
#endif
#ifndef BuildType
  #define BuildType "Nightly"
#endif
#define MyAppName "Ditto"
#define MyAppVersion GetFileVersion(PackagePath + "\Ditto.exe")
#define PackageVersion MyAppVersion + "-arm64-" + BuildLabel

[Setup]
; The x64 installer implicitly uses AppName as AppId. Keep the same identity.
AppId=Ditto
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName=Ditto {#MyAppVersion} (ARM64 {#BuildType} Build)
AppPublisher=steathy (unofficial ARM64 build)
AppPublisherURL=https://github.com/steathy/ditto-arm64
AppSupportURL=https://github.com/steathy/ditto-arm64/issues
AppUpdatesURL=https://github.com/steathy/ditto-arm64/releases
OutputBaseFilename=DittoSetup_{#PackageVersion}
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
DefaultDirName={autopf}\Ditto
DefaultGroupName=Ditto
PrivilegesRequired=admin
UsePreviousAppDir=yes
UsePreviousTasks=yes
DisableProgramGroupPage=yes
DirExistsWarning=no
UninstallLogMode=overwrite
UninstallDisplayName=Ditto (Windows ARM64)
UninstallDisplayIcon={app}\Ditto.exe
VersionInfoDescription=Ditto ARM64 {#BuildType} installer
VersionInfoVersion={#MyAppVersion}
VersionInfoProductName=Ditto ARM64
VersionInfoProductVersion={#MyAppVersion}
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no
MinVersion=10.0.22000
SetupLogging=yes
LicenseFile=..\LICENSE

[Languages]
Name: English; MessagesFile: compiler:Default.isl

;Inno Setup Official translations
Name: Czech; MessagesFile: compiler:Languages\Czech.isl
Name: Danish; MessagesFile: compiler:Languages\Danish.isl
Name: Dutch; MessagesFile: compiler:Languages\Dutch.isl
Name: Finnish; MessagesFile: compiler:Languages\Finnish.isl
Name: French; MessagesFile: compiler:Languages\French.isl
Name: German; MessagesFile: compiler:Languages\German.isl
Name: Hebrew; MessagesFile: compiler:Languages\Hebrew.isl
Name: Italiano; MessagesFile: compiler:Languages\Italian.isl
Name: Japanese; MessagesFile: compiler:Languages\Japanese.isl
Name: Polish; MessagesFile: compiler:Languages\Polish.isl
Name: Portuguese_Brazil; MessagesFile: compiler:Languages\BrazilianPortuguese.isl
Name: Portuguese_Portugal; MessagesFile: compiler:Languages\Portuguese.isl
Name: Russian; MessagesFile: compiler:Languages\Russian.isl
Name: Slovak; MessagesFile: compiler:Languages\Slovak.isl
Name: Slovenian; MessagesFile: compiler:Languages\Slovenian.isl
Name: Spanish; MessagesFile: compiler:Languages\Spanish.isl
Name: Turkish; MessagesFile: compiler:Languages\Turkish.isl
Name: Ukrainian; MessagesFile: compiler:Languages\Ukrainian.isl

;Inno Setup Unofficial translations
Name: Simplified_Chinese; MessagesFile: ChineseSimplified.isl
Name: Traditional_Chinese; MessagesFile: ChineseTraditional.isl
Name: Croatian; MessagesFile: Croatian.isl
Name: Greek; MessagesFile: Greek.isl
Name: Hungarian; MessagesFile: Hungarian.isl
Name: Korean; MessagesFile: Korean.isl
Name: Romanian; MessagesFile: Romanian.isl
Name: Swedish; MessagesFile: Swedish.isl

[Tasks]
Name: RunAtStartup; Description: Run Ditto on Windows startup
Name: AddFireWallException; Description: Allow Ditto network sharing on TCP port 23443; Flags: unchecked

[Files]
Source: {#PackagePath}\Ditto.exe; DestDir: {app}; Flags: ignoreversion
Source: {#PackagePath}\DittoUpdate.exe; DestDir: {app}; Flags: ignoreversion
Source: {#PackagePath}\ICU_Loader.dll; DestDir: {app}; Flags: ignoreversion
Source: {#PackagePath}\Addins\DittoUtil.dll; DestDir: {app}\Addins; Flags: ignoreversion
Source: {#PackagePath}\mfc140u.dll; DestDir: {app}; Flags: ignoreversion
Source: {#PackagePath}\msvcp140.dll; DestDir: {app}; Flags: ignoreversion
Source: {#PackagePath}\vcruntime140.dll; DestDir: {app}; Flags: ignoreversion
Source: {#PackagePath}\Language\*.xml; DestDir: {app}\Language; Flags: ignoreversion
Source: {#PackagePath}\Themes\*.xml; DestDir: {app}\Themes; Flags: ignoreversion
Source: {#PackagePath}\LICENSE; DestDir: {app}; Flags: ignoreversion
Source: {#PackagePath}\zlib-LICENSE.txt; DestDir: {app}; Flags: ignoreversion
Source: {#PackagePath}\build-info.json; DestDir: {app}; DestName: arm64-build-info.json; Flags: ignoreversion
; Never ship portable, Ditto.Settings or a database in an installed build.

[Icons]
Name: {group}\Ditto; Filename: {app}\Ditto.exe
Name: {group}\Uninstall; Filename: {uninstallexe}

[Run]
Filename: {app}\Ditto.exe; Description: Launch Ditto; Flags: nowait postinstall skipifsilent runasoriginaluser

[Registry]
; Preserve all Ditto preferences, including the user's language and custom keys.
; Uninstall deliberately retains HKCU\Software\Ditto and the user's database.
Root: HKCU; Subkey: SOFTWARE\Microsoft\Windows\CurrentVersion\Run; ValueType: string; ValueName: Ditto; ValueData: """{app}\Ditto.exe"""; Tasks: RunAtStartup; Flags: createvalueifdoesntexist uninsdeletevalue
Root: HKCR; Subkey: .dto; ValueType: string; ValueData: Ditto; Flags: uninsdeletevalue
Root: HKCR; Subkey: Ditto; ValueType: string; ValueData: Ditto; Flags: uninsdeletekey
Root: HKCR; Subkey: Ditto\DefaultIcon; ValueType: string; ValueData: {app}\Ditto.exe,0
Root: HKCR; Subkey: Ditto\shell\open\command; ValueType: string; ValueData: """{app}\Ditto.exe"" ""%1"""

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  RuleName: String;
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('AddFireWallException') then
  begin
    RuleName := 'Ditto_FromInstaller_64';
    if Exec(ExpandConstant('{sys}\netsh.exe'),
      'advfirewall firewall show rule name="' + RuleName + '"',
      '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0) then
      Exit;
    if not Exec(ExpandConstant('{sys}\netsh.exe'),
      'advfirewall firewall add rule name="' + RuleName +
      '" dir=in action=allow protocol=TCP localport=23443 program="' +
      ExpandConstant('{app}\Ditto.exe') + '" enable=yes',
      '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
      Log('Ditto firewall rule could not be added.');
  end;
end;
