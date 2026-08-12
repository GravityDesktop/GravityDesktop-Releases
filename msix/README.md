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

## Prototype identity

The manifest uses a deliberately non-production identity:

- `Name`: `GravityDesktop.Prototype`
- `Publisher`: `CN=GravityDesktop Prototype`
- `Version`: `0.1.0.0`

Before a Partner Center submission, replace the identity `Name` and `Publisher`
with the exact values reserved for the GravityDesktop product. MSIX versions
are numeric, so the package uses `0.1.0.0`; the displayed product version may
continue to be `0.1.0-alpha`.

## Building an unsigned prototype package

1. Build the existing x64 Release binaries.
2. Run `./msix/Build-MsixPrototype.ps1` from the repository root.

The script writes only under `out/msix-prototype`, which is ignored by Git.
It creates an unsigned MSIX package for structure inspection. Sideloading it
requires a trusted development certificate. Store submission uses the Partner
Center-reserved identity and Store signing workflow instead.

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
