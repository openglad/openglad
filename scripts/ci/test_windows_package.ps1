# Smoke the release ZIP as a downloaded user would: extract it away from the
# build tree, then launch both executables with no MSYS2 paths available.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File scripts/ci/test_windows_package.ps1 -ZipPath openglad-windows-x86_64.zip
# ASCII only: Windows PowerShell 5.1 reads a script without a BOM as ANSI.
param(
    [Parameter(Mandatory = $true)][string]$ZipPath
)

$ErrorActionPreference = 'Stop'
$zip = (Resolve-Path -LiteralPath $ZipPath).ProviderPath
$extract = Join-Path ([System.IO.Path]::GetTempPath()) ("openglad-package-" + [guid]::NewGuid().ToString('N'))

# Child processes inherit this error mode. A missing import then returns the
# loader status instead of opening a modal Windows error dialog in CI.
Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class Win32ErrorMode {
    [DllImport("kernel32.dll")]
    public static extern uint SetErrorMode(uint mode);
}
'@

function Invoke-Help([string]$exe, [string]$directory) {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $exe
    $start.Arguments = '-h'
    $start.WorkingDirectory = $directory
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    # Existing user campaigns or packs must not fill holes in the archive.
    $start.EnvironmentVariables['OPENGLAD_CONFIG_DIR'] = (Join-Path (Split-Path -Parent $directory) 'user').Replace('\', '/')

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw "Could not start $exe" }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(15000)) {
            $process.Kill()
            [void]$process.WaitForExit(5000)
            throw "$exe -h did not exit within 15 seconds"
        }
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        }
    } finally {
        $process.Dispose()
    }
}

function Assert-PackageHelp([string]$package, [string]$stage) {
    foreach ($name in @('openglad.exe', 'openscen.exe')) {
        $exe = Join-Path $package $name
        if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
            throw "ZIP is missing $name"
        }
        $result = Invoke-Help $exe $package
        if ($result.ExitCode -ne 0 -or $result.Output -notmatch '(?m)^Usage: openglad \[-d -f \.\.\.\]\r?$' -or
            $result.Output -notmatch '(?m)^\s+-h\s+Print a summary of the options') {
            throw "$name -h failed ($stage): exit=$($result.ExitCode); output=$($result.Output)"
        }
        Write-Host "$name -h ($stage): exit 0, expected usage printed"
    }
}

$oldPath = $env:PATH
$oldErrorMode = [Win32ErrorMode]::SetErrorMode(0x0003) # SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX
try {
    New-Item -ItemType Directory -Path $extract | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $extract 'user') | Out-Null
    Expand-Archive -LiteralPath $zip -DestinationPath $extract
    $package = Join-Path $extract 'openglad'
    if (-not (Test-Path -LiteralPath $package -PathType Container)) {
        throw 'ZIP must contain an openglad/ directory'
    }

    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    Assert-PackageHelp $package 'before missing-DLL check'

    # Prove the launch oracle catches the reported class of packaging error.
    # Rename only inside the temporary extraction, and require the exact NT
    # loader status for a missing imported DLL (STATUS_DLL_NOT_FOUND).
    $physfs = @(Get-ChildItem -LiteralPath $package -File -Filter 'libphysfs*.dll')
    if ($physfs.Count -ne 1) {
        throw "Expected one bundled libphysfs DLL for the planted break; found $($physfs.Count)"
    }
    $hidden = $physfs[0].FullName + '.removed-for-smoke'
    Move-Item -LiteralPath $physfs[0].FullName -Destination $hidden
    try {
        foreach ($name in @('openglad.exe', 'openscen.exe')) {
            $result = Invoke-Help (Join-Path $package $name) $package
            if ($result.ExitCode -ne -1073741515) { # 0xC0000135
                throw "Missing libphysfs did not trigger STATUS_DLL_NOT_FOUND for ${name}: exit=$($result.ExitCode); output=$($result.Output)"
            }
            Write-Host "$name with libphysfs removed: STATUS_DLL_NOT_FOUND as expected"
        }
    } finally {
        Move-Item -LiteralPath $hidden -Destination $physfs[0].FullName
    }
    Assert-PackageHelp $package 'after DLL restore'
} finally {
    $env:PATH = $oldPath
    [void][Win32ErrorMode]::SetErrorMode($oldErrorMode)
    if (Test-Path -LiteralPath $extract) {
        Remove-Item -LiteralPath $extract -Recurse -Force
    }
}
