param(
    [Parameter(Mandatory = $true)]
    [string]$PackageFamilyName,

    [string]$ApplicationId = 'GravityDesktop',

    [string]$Arguments = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($PackageFamilyName) -or
    [string]::IsNullOrWhiteSpace($ApplicationId)) {
    throw 'PackageFamilyName and ApplicationId are required.'
}

# MSIX applications must be started through their registered AUMID.  A bare
# executable name is neither an MSIX activation contract nor a stable path.
$aumid = "$PackageFamilyName!$ApplicationId"

$signature = @'
using System;
using System.Runtime.InteropServices;

[ComImport, Guid("45BA127D-10A8-46EA-8AB7-56EA9078943C")]
public class ApplicationActivationManager {}

[ComImport, Guid("2E941141-7F97-4756-BA1D-9DECDE894A3D"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IApplicationActivationManager {
    int ActivateApplication(
        [MarshalAs(UnmanagedType.LPWStr)] string appUserModelId,
        [MarshalAs(UnmanagedType.LPWStr)] string arguments,
        uint options,
        out uint processId);
}

public static class AppActivator {
    public static uint Activate(string appUserModelId, string arguments) {
        IApplicationActivationManager manager =
            (IApplicationActivationManager)new ApplicationActivationManager();
        uint processId;
        int hr = manager.ActivateApplication(
            appUserModelId, arguments ?? String.Empty, 0, out processId);
        if (hr < 0) Marshal.ThrowExceptionForHR(hr);
        return processId;
    }
}
'@

if (-not ('GravityDesktopMsix.IApplicationActivationManager' -as [type])) {
    Add-Type -TypeDefinition ("namespace GravityDesktopMsix {" + $signature + "}")
}

$processId = [GravityDesktopMsix.AppActivator]::Activate($aumid, $Arguments)

[pscustomobject]@{
    Aumid = $aumid
    ProcessId = $processId
}
