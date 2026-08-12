# Gravity Desktop MSIX prototype

This folder is independent from the existing Inno Setup release chain. It does
not replace `installer/GravityDesktop-Setup.iss` or alter application behavior.

## Contents

- `GravityDesktop.Package.wapproj`: Windows Application Packaging Project for
  Visual Studio once the Desktop Bridge / MSIX Packaging workload is installed.
- `Package.appxmanifest`: x64 Windows 11 desktop manifest with `runFullTrust`.
  It exposes separate launch entries for Gravity Desktop and Gravity Desktop
  Recovery.
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

1. Build the existing x64 Release binaries.
2. Run `./msix/Build-MsixPrototype.ps1` from the repository root.

The script writes only under `out/msix-prototype`, which is ignored by Git. It
creates an unsigned MSIX package for structure inspection. Sideloading it
requires a trusted development certificate. Store submission uses this Partner
Center-reserved identity and the Store signing workflow.

## Compatibility boundaries

- The current app remains a full-trust desktop app and retains its existing
  `%LocalAppData%\GravityDesktop` state/preference location and Documents
  managed library. This prototype does not migrate, virtualize, delete, or
  rewrite user data.
- Existing HKCU Run autostart remains untouched. A future Store-focused variant
  should evaluate `windows.startupTask`; it must not be silently substituted
  for the current behavior.
- Desktop-host attachment, interaction islands, click-through, tray behavior,
  Explorer recovery, Win+D, DPI, and monitor behavior require installation
  testing under package identity. No packaging-only change is assumed to make
  those host-sensitive behaviors safe.
