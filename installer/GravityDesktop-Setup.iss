; Gravity Desktop internal test installer.
; This script intentionally packages only the clean Release executable.

#define MyAppName "引力桌面"
#define MyAppEnglishName "Gravity Desktop"
#define MyAppVersion "0.1.0-alpha"
#define MyAppPublisher "wangboyun"
#define MyAppWebsite "https://gravitydesktop.com"
#define MyAppExeName "GravityDesktop.exe"

[Setup]
AppId={{CF0B84D4-4B86-497C-A89C-5B4F49AB2D64}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppWebsite}
AppSupportURL={#MyAppWebsite}
AppUpdatesURL={#MyAppWebsite}
AppComments=桌面文件工作关系整理工具
DefaultDirName={localappdata}\Programs\Gravity Desktop
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
DisableWelcomePage=no
ShowLanguageDialog=no
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\out\installer
OutputBaseFilename=GravityDesktop-Setup-0.1.0-alpha
SetupIconFile=..\src\GravityDesktop.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
VersionInfoVersion=0.1.0.0
VersionInfoProductVersion=0.1.0.0
VersionInfoProductName={#MyAppName}
VersionInfoDescription=桌面文件工作关系整理工具
VersionInfoCompany={#MyAppPublisher}
VersionInfoCopyright=© 2026 wangboyun. All rights reserved.
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
CloseApplicationsFilter={#MyAppExeName},GravityDesktopRecovery.exe
RestartApplications=no
AppMutex=Local\GravityDesktop.SingleInstance.9D19E743,Local\GravityDesktop.EmergencyRecovery.9D19E743

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Messages]
SetupAppTitle=引力桌面安装程序
SetupWindowTitle=引力桌面安装程序
SetupAppRunningError=检测到引力桌面或紧急恢复程序正在运行。请先安全退出，然后继续安装。
UninstallAppRunningError=检测到引力桌面或紧急恢复程序正在运行。请先安全退出，然后继续卸载。

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式："; Flags: unchecked
Name: "autostart"; Description: "开机时自动启动引力桌面"; GroupDescription: "启动选项："; Flags: unchecked

[Files]
Source: "..\out\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\out\Release\GravityDesktopRecovery.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\out\Release\GravityDesktopLibrary.ico"; DestDir: "{app}"; Flags: ignoreversion
; Localized UI resources are loaded beside the executables. Keep this as a
; directory copy so every shipped language is available after installation.
Source: "..\out\Release\locales\*"; DestDir: "{app}\locales"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{userprograms}\{#MyAppName}\紧急恢复文件"; Filename: "{app}\GravityDesktopRecovery.exe"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{userdesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "GravityDesktop"; ValueData: """{app}\{#MyAppExeName}"" --autostart"; Flags: uninsdeletevalue; Tasks: autostart

[UninstallRun]
Filename: "{app}\{#MyAppExeName}"; Parameters: "--uninstall-cleanup"; Flags: runhidden waituntilterminated; RunOnceId: "GravityDesktopAutoStartCleanup"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "启动引力桌面"; Flags: nowait postinstall skipifsilent

[Code]
function InitializeUninstall(): Boolean;
begin
  // The user can select a custom managed-library path. Always state the
  // protection rule instead of inspecting any user library during uninstall.
  MsgBox('引力桌面仍保存着托管文件。卸载程序不会删除这些文件。',
    mbInformation, MB_OK);
  Result := True;
end;
