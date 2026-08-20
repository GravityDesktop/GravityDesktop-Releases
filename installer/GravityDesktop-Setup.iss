; Gravity Desktop internal test installer.
; This script intentionally packages only the clean Release executable.

#define MyAppName "Gravity Desktop"
#define MyAppEnglishName "Gravity Desktop"
#define MyAppPublisher "wangboyun"
#define MyAppWebsite "https://gravitydesktop.com"
#define MyAppExeName "GravityDesktop.exe"
#define MyAppBinary AddBackslash(SourcePath) + "..\out\Release\GravityDesktop.exe"
#define MyAppVersion GetFileProductVersionString(MyAppBinary)
#define MyAppFileVersion GetVersionNumbersString(MyAppBinary)

[Setup]
AppId={{CF0B84D4-4B86-497C-A89C-5B4F49AB2D64}
AppName={cm:ProductName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppWebsite}
AppSupportURL={#MyAppWebsite}
AppUpdatesURL={#MyAppWebsite}
AppComments=Desktop file work relationship organizer
DefaultDirName={localappdata}\Programs\Gravity Desktop
DefaultGroupName={cm:ProductName}
DisableProgramGroupPage=yes
DisableWelcomePage=no
ShowLanguageDialog=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\out\installer
OutputBaseFilename=GravityDesktop-Setup-{#MyAppVersion}
SetupIconFile=..\src\GravityDesktop.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
VersionInfoVersion={#MyAppFileVersion}
VersionInfoProductVersion={#MyAppFileVersion}
VersionInfoProductName=Gravity Desktop
VersionInfoDescription=Desktop file work relationship organizer
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
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "chinesetrad"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Messages]
SetupAppTitle={cm:InstallerTitle}
SetupWindowTitle={cm:InstallerTitle}
SetupAppRunningError={cm:AppRunningError}
UninstallAppRunningError={cm:UninstallRunningError}

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopShortcut}"; GroupDescription: "{cm:AdditionalShortcuts}"; Flags: unchecked
Name: "autostart"; Description: "{cm:StartWithWindows}"; GroupDescription: "{cm:StartupOptions}"; Flags: unchecked

[Files]
Source: "..\out\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\out\Release\GravityDesktopRecovery.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\out\Release\GravityDesktopLibrary.ico"; DestDir: "{app}"; Flags: ignoreversion
; Localized UI resources are loaded beside the executables. Keep this as a
; directory copy so every shipped language is available after installation.
Source: "..\out\Release\locales\*"; DestDir: "{app}\locales"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\{cm:ProductName}"; Filename: "{app}\{#MyAppExeName}"; Parameters: "{code:InitialLanguageParameter}"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{userprograms}\{cm:ProductName}\{cm:EmergencyRecovery}"; Filename: "{app}\GravityDesktopRecovery.exe"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{userdesktop}\{cm:ProductName}"; Filename: "{app}\{#MyAppExeName}"; Parameters: "{code:InitialLanguageParameter}"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "GravityDesktop"; ValueData: """{app}\{#MyAppExeName}"" --autostart {code:InitialLanguageParameter}"; Flags: uninsdeletevalue; Tasks: autostart

[UninstallRun]
Filename: "{app}\{#MyAppExeName}"; Parameters: "--uninstall-cleanup"; Flags: runhidden waituntilterminated; RunOnceId: "GravityDesktopAutoStartCleanup"

[Run]
Filename: "{app}\{#MyAppExeName}"; Parameters: "{code:InitialLanguageParameter}"; Description: "{cm:LaunchGravityDesktop}"; Flags: nowait postinstall skipifsilent

[CustomMessages]
english.ProductName=Gravity Desktop
english.EmergencyRecovery=Emergency File Recovery
english.CreateDesktopShortcut=Create a desktop shortcut
english.AdditionalShortcuts=Additional shortcuts:
english.StartWithWindows=Start Gravity Desktop when I sign in to Windows
english.StartupOptions=Startup options:
english.LaunchGravityDesktop=Launch Gravity Desktop
english.InstallerTitle=Gravity Desktop Setup
english.AppRunningError=Gravity Desktop or Emergency File Recovery is still running. Exit it safely, then continue installation.
english.UninstallRunningError=Gravity Desktop or Emergency File Recovery is still running. Exit it safely, then continue uninstalling.
english.UninstallManagedFilesNotice=Gravity Desktop still has managed files. The uninstaller will not delete them.
chinesesimp.ProductName=引力桌面
chinesesimp.EmergencyRecovery=紧急恢复文件
chinesesimp.CreateDesktopShortcut=创建桌面快捷方式
chinesesimp.AdditionalShortcuts=附加快捷方式：
chinesesimp.StartWithWindows=登录 Windows 后启动引力桌面
chinesesimp.StartupOptions=启动选项：
chinesesimp.LaunchGravityDesktop=启动引力桌面
chinesesimp.InstallerTitle=引力桌面安装程序
chinesesimp.AppRunningError=检测到引力桌面或紧急恢复程序正在运行。请先安全退出，然后继续安装。
chinesesimp.UninstallRunningError=检测到引力桌面或紧急恢复程序正在运行。请先安全退出，然后继续卸载。
chinesesimp.UninstallManagedFilesNotice=引力桌面仍保存着托管文件。卸载程序不会删除这些文件。
chinesetrad.ProductName=引力桌面
chinesetrad.EmergencyRecovery=緊急復原檔案
chinesetrad.CreateDesktopShortcut=建立桌面捷徑
chinesetrad.AdditionalShortcuts=其他捷徑：
chinesetrad.StartWithWindows=登入 Windows 後啟動引力桌面
chinesetrad.StartupOptions=啟動選項：
chinesetrad.LaunchGravityDesktop=啟動引力桌面
chinesetrad.InstallerTitle=引力桌面安裝程式
chinesetrad.AppRunningError=偵測到引力桌面或緊急復原程式正在執行。請先安全結束，再繼續安裝。
chinesetrad.UninstallRunningError=偵測到引力桌面或緊急復原程式正在執行。請先安全結束，再繼續解除安裝。
chinesetrad.UninstallManagedFilesNotice=引力桌面仍保存受管理檔案。解除安裝程式不會刪除這些檔案。
japanese.ProductName=Gravity Desktop
japanese.EmergencyRecovery=緊急ファイル復元
japanese.CreateDesktopShortcut=デスクトップ ショートカットを作成する
japanese.AdditionalShortcuts=追加のショートカット:
japanese.StartWithWindows=Windows へのサインイン時に Gravity Desktop を起動する
japanese.StartupOptions=起動オプション:
japanese.LaunchGravityDesktop=Gravity Desktop を起動する
japanese.InstallerTitle=Gravity Desktop セットアップ
japanese.AppRunningError=Gravity Desktop または緊急ファイル復元が実行中です。安全に終了してからインストールを続行してください。
japanese.UninstallRunningError=Gravity Desktop または緊急ファイル復元が実行中です。安全に終了してからアンインストールを続行してください。
japanese.UninstallManagedFilesNotice=Gravity Desktop には管理対象のファイルが残っています。アンインストーラーはそれらを削除しません。
german.ProductName=Gravity Desktop
german.EmergencyRecovery=Dateinotfallwiederherstellung
german.CreateDesktopShortcut=Desktopverknüpfung erstellen
german.AdditionalShortcuts=Zusätzliche Verknüpfungen:
german.StartWithWindows=Gravity Desktop bei der Windows-Anmeldung starten
german.StartupOptions=Startoptionen:
german.LaunchGravityDesktop=Gravity Desktop starten
german.InstallerTitle=Gravity Desktop Setup
german.AppRunningError=Gravity Desktop oder die Dateinotfallwiederherstellung wird noch ausgeführt. Beenden Sie sie sicher und setzen Sie die Installation fort.
german.UninstallRunningError=Gravity Desktop oder die Dateinotfallwiederherstellung wird noch ausgeführt. Beenden Sie sie sicher und setzen Sie die Deinstallation fort.
german.UninstallManagedFilesNotice=Gravity Desktop enthält weiterhin verwaltete Dateien. Das Deinstallationsprogramm löscht sie nicht.
spanish.ProductName=Gravity Desktop
spanish.EmergencyRecovery=Recuperación de archivos de emergencia
spanish.CreateDesktopShortcut=Crear un acceso directo en el escritorio
spanish.AdditionalShortcuts=Accesos directos adicionales:
spanish.StartWithWindows=Iniciar Gravity Desktop al iniciar sesión en Windows
spanish.StartupOptions=Opciones de inicio:
spanish.LaunchGravityDesktop=Iniciar Gravity Desktop
spanish.InstallerTitle=Instalación de Gravity Desktop
spanish.AppRunningError=Gravity Desktop o Recuperación de archivos de emergencia sigue en ejecución. Ciérrelo de forma segura y continúe con la instalación.
spanish.UninstallRunningError=Gravity Desktop o Recuperación de archivos de emergencia sigue en ejecución. Ciérrelo de forma segura y continúe con la desinstalación.
spanish.UninstallManagedFilesNotice=Gravity Desktop todavía contiene archivos administrados. El desinstalador no los eliminará.
french.ProductName=Gravity Desktop
french.EmergencyRecovery=Récupération d’urgence des fichiers
french.CreateDesktopShortcut=Créer un raccourci sur le Bureau
french.AdditionalShortcuts=Raccourcis supplémentaires :
french.StartWithWindows=Démarrer Gravity Desktop à l’ouverture de session Windows
french.StartupOptions=Options de démarrage :
french.LaunchGravityDesktop=Lancer Gravity Desktop
french.InstallerTitle=Installation de Gravity Desktop
french.AppRunningError=Gravity Desktop ou la récupération d’urgence des fichiers est toujours en cours d’exécution. Fermez-la correctement, puis poursuivez l’installation.
french.UninstallRunningError=Gravity Desktop ou la récupération d’urgence des fichiers est toujours en cours d’exécution. Fermez-la correctement, puis poursuivez la désinstallation.
french.UninstallManagedFilesNotice=Gravity Desktop contient encore des fichiers gérés. Le programme de désinstallation ne les supprimera pas.

[Code]
function InitializeUninstall(): Boolean;
begin
  // The user can select a custom managed-library path. Always state the
  // protection rule instead of inspecting any user library during uninstall.
  MsgBox(ExpandConstant('{cm:UninstallManagedFilesNotice}'),
    mbInformation, MB_OK);
  Result := True;
end;

function InitialLanguageParameter(Param: String): String;
begin
  if ActiveLanguage = 'chinesesimp' then Result := '--initial-language=zh-CN'
  else if ActiveLanguage = 'chinesetrad' then Result := '--initial-language=zh-TW'
  else if ActiveLanguage = 'japanese' then Result := '--initial-language=ja-JP'
  else if ActiveLanguage = 'german' then Result := '--initial-language=de-DE'
  else if ActiveLanguage = 'spanish' then Result := '--initial-language=es-ES'
  else if ActiveLanguage = 'french' then Result := '--initial-language=fr-FR'
  else Result := '--initial-language=en-US';
end;
