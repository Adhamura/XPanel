; XPanel installer for Windows x64 (Inno Setup 6). Built by packaging/windows/build-release.ps1:
;   iscc /DVersion=1.0.4 /DSourceDir=<windeployqt folder> /DOutputDir=dist xpanel.iss
; Windows support is experimental: TDP / Operating Mode, fan, charge limit, Aura lighting and
; boot sound through the ASUS System Control Interface; no controller remapping yet.
#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build-release-win\stage"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{6090E766-5D01-42C4-9315-CF35E7DD9ACD}
AppName=XPanel
AppVersion={#Version}
AppVerName=XPanel {#Version}
AppPublisher=XPanel (independent project, not affiliated with ASUS)
AppPublisherURL=https://github.com/Adhamura/XPanel
AppSupportURL=https://github.com/Adhamura/XPanel/issues
DefaultDirName={autopf}\XPanel
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=XPanel-{#Version}-Windows-x64-Setup
SetupIconFile=xpanel.ico
UninstallDisplayIcon={app}\XPanel.exe
LicenseFile=..\..\LICENSE
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=force

[Messages]
WelcomeLabel2=This installs XPanel {#Version}, a Quick Panel and Control Center for the ASUS ROG Ally X.%n%nWindows support is experimental: Operating Mode / TDP, fan, charge limit, lighting and boot sound work through the ASUS System Control Interface; controller remapping is Linux-only for now.%n%nArmoury Crate changes the same settings: close it (or uninstall it) while you use XPanel.

[Tasks]
Name: autostart; Description: "Start XPanel when I sign in (tray icon, keeps your settings applied)"; GroupDescription: "Startup:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{autoprograms}\XPanel"; Filename: "{app}\XPanel.exe"; Parameters: "--app"
Name: "{autoprograms}\XPanel Quick Panel"; Filename: "{app}\XPanel.exe"; Parameters: "--toggle"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "XPanel"; ValueData: """{app}\XPanel.exe"" --background"; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\XPanel.exe"; Parameters: "--app"; Description: "Start XPanel"; Flags: nowait postinstall skipifsilent runasoriginaluser
; XPanel's own updater runs Setup with /SILENT ... /relaunch=1 and quits: start it again when done,
; as the signed-in user (not elevated). Other silent installs (scripts, winget) do not start it.
Filename: "{app}\XPanel.exe"; Parameters: "--app settings/update"; Flags: nowait runasoriginaluser; Check: RelaunchAfterUpdate

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM XPanel.exe"; Flags: runhidden; RunOnceId: "StopXPanel"
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM xpanel-daemon.exe"; Flags: runhidden; RunOnceId: "StopDaemon"

[Code]
function RelaunchAfterUpdate: Boolean;
begin
  Result := WizardSilent and (ExpandConstant('{param:relaunch|0}') = '1');
end;
