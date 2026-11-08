; Inno Setup script for ED Overlay Board (per-user install, no admin rights needed).
; Build: installer\build-installer.ps1  (or: iscc /DAppVersion=x.y.z /DSourceDir=<cmake --install folder> EDOverlayBoard.iss)

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\EDOverlayBoard"
#endif

#define AppName "ED Overlay Board"
#define AppExe "EDOverlayBoard.exe"

[Setup]
AppId={{E00EA254-FCAD-4732-973C-E30B9F1952D5}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Andreas Rettig
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; per user: installs to %LOCALAPPDATA%\Programs, the OpenXR layer is registered for the current user
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
LicenseFile=..\LICENSE
SetupIconFile=..\resources\EDOverlayBoard.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir=..\dist
OutputBaseFilename=EDOverlayBoard-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"; InfoBeforeFile: "info-de.txt"
Name: "en"; MessagesFile: "compiler:Default.isl"; InfoBeforeFile: "info-en.txt"

[CustomMessages]
de.Autostart=Mit Windows starten
en.Autostart=Start with Windows
de.LaunchApp=ED Overlay Board jetzt starten
en.LaunchApp=Start ED Overlay Board now
de.RemoveLayer=OpenXR-Layer wird entfernt ...
en.RemoveLayer=Removing the OpenXR layer ...

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "{cm:Autostart}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\EDOverlayBoard_Layer.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\EDOverlayBoard_Layer.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\resources\*"; DestDir: "{app}\resources"; Flags: ignoreversion recursesubdirs
Source: "{#SourceDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs
Source: "{#SourceDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Dirs]
; image folder of the example "Klemmbrett" window (config.json is created on the first start)
Name: "{app}\klemmbrett"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon
Name: "{userstartup}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: autostart

[Run]
Filename: "{app}\{#AppExe}"; Parameters: "--install-layer"; Flags: runhidden waituntilterminated
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchApp}"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM {#AppExe}"; Flags: runhidden; RunOnceId: "StopApp"
Filename: "{app}\{#AppExe}"; Parameters: "--uninstall-layer"; Flags: runhidden waituntilterminated; StatusMsg: "{cm:RemoveLayer}"; RunOnceId: "RemoveLayer"

[UninstallDelete]
; config.json and the image folder stay (your settings and pages); only the log goes
Type: files; Name: "{app}\EDOverlayBoard.log"

[Code]
// Close a running instance first (it sits in the tray and keeps the exe locked).
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM {#AppExe}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := '';
end;
