# Gravity Desktop MSIX prototype

This folder is independent from the existing Inno Setup release chain. It does
not replace `installer/GravityDesktop-Setup.iss` or alter application behavior.

## Contents

- `GravityDesktop.Package.wapproj`: Windows Application Packaging Project for
  Visual Studio once the Desktop Bridge / MSIX Packaging workload is installed.
- `Package.appxmanifest`: x64 Windows 11 desktop manifest with `runFullTrust`.
  It exposes one Store entry for Gravity Desktop. `GravityDesktopRecovery.exe`
  remains package payload and is copied to the existing durable safety-center
  location only when its content changes, preserving independent emergency
  recovery without defining a second Store application.
- `Assets`: transparent high-resolution logo assets. The Square44 resource has
  target-size and DPI-scale variants so the shell can select an exact taskbar,
  Alt+Tab, Start, Search, and Task View icon instead of enlarging one small
  bitmap. The single Store application uses this branded resource family.
- `Build-MsixPrototype.ps1`: Windows SDK MakeAppx fallback that stages the
  current Release binaries, six runtime locale resources, and package assets.

## Store identity

The manifest uses the Microsoft Partner Center identity reserved for this app:

- `Name`: `wbyhome.GravityDesktop`
- `Publisher`: `CN=00AA024B-6408-4EF0-B53F-F515553B964E`
- `PublisherDisplayName`: `wbyhome`
- `Version`: `0.1.0.0`

MSIX versions are numeric, so the package uses `0.1.0.0`; the displayed product
version may continue to be `0.1.0-alpha`.

## Building an unsigned prototype package

1. Build the Store-compatible x64 binary with
   `MSBuild GravityDesktop.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:GravityDesktopPackaged=true`.
2. Run `./msix/Build-MsixPrototype.ps1 -PackagedBuild` from the repository root.

The normal EXE/Inno build remains `Release`. The `ReleaseMSIX` build switches
only the platform launcher boundary to Windows Runtime Launcher APIs; it does
not change workspace, file-management, or recovery semantics.

The script writes only under `out/msix-prototype`, which is ignored by Git. It
creates an unsigned MSIX package for structure inspection. Sideloading it
requires a trusted development certificate. Store submission uses this Partner
Center-reserved identity and the Store signing workflow.

## Activation and recovery entry

The only package activation entry is the AUMID derived from the package family
name and `GravityDesktop`: `wbyhome.GravityDesktop_4j298z9ac9h26!GravityDesktop`
for the current Store identity. Start and test the MSIX through that registered
application entry. Do **not** start `GravityDesktop.exe` or
`GravityDesktopRecovery.exe` by bare filename, `Invoke-CommandInDesktopPackage
-Command`, a working-directory-relative command, or a stale AppsFolder entry.
Those are not MSIX activation contracts and can produce Windows "file not found"
errors.

Use `Invoke-MsixApp.ps1 -PackageFamilyName <PFN>` for a controlled local
activation test. It uses `IApplicationActivationManager::ActivateApplication`
with the current package family name and the declared application ID; it does
not invoke an executable by filename.

`GravityDesktopRecovery.exe` remains an internal package payload. After the
main Store app has run once, the existing safety-center provisioning creates its
verified durable recovery copy under the Documents managed library; its
`恢复全部文件到桌面` shortcut starts that durable copy. It never restarts the
main application after recovery. The Inno Setup edition still exposes its own
independent Recovery shortcut directly.

## Compatibility boundaries

- The current app remains a full-trust desktop app and retains its existing
  `%LocalAppData%\GravityDesktop` state/preference location and Documents
  managed library. This prototype does not migrate, virtualize, delete, or
  rewrite user data.
- The unpackaged EXE keeps HKCU Run. Package-identity execution uses the
  manifest-declared `windows.startupTask`, so the two registration paths are
  never used together.
- Desktop-host attachment, interaction islands, click-through, tray behavior,
  Explorer recovery, Win+D, DPI, and monitor behavior require installation
  testing under package identity. No packaging-only change is assumed to make
  those host-sensitive behaviors safe.
