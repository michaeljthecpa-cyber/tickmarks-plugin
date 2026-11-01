<#
  Tickmarks plug-in for Adobe Acrobat - installer.
  Copies Tickmarks.api into Acrobat's plug_ins folder (needs admin; Install.cmd asks for it)
  and removes the older script version of Tickmarks if it is present.
#>
$ErrorActionPreference = 'Stop'
# Re-launch with admin rights if needed (Acrobat's plug_ins folder is under Program Files).
$me = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $me.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell -Verb RunAs -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-NoExit', '-File', "`"$PSCommandPath`"")
    exit
}
$src = Split-Path -Parent $MyInvocation.MyCommand.Path
Write-Host "Tickmarks plug-in installer" -ForegroundColor Cyan

$candidates = @(
    @{ Dir = "$env:ProgramFiles\Adobe\Acrobat DC\Acrobat";        Arch = 'x64' },
    @{ Dir = "${env:ProgramFiles(x86)}\Adobe\Acrobat DC\Acrobat"; Arch = 'x86' },
    @{ Dir = "$env:ProgramFiles\Adobe\Acrobat 2020\Acrobat";      Arch = 'x64' },
    @{ Dir = "${env:ProgramFiles(x86)}\Adobe\Acrobat 2020\Acrobat"; Arch = 'x86' }
) | Where-Object { $_.Dir -and (Test-Path (Join-Path $_.Dir 'Acrobat.exe')) }
if (-not $candidates) { throw "Could not find Adobe Acrobat (Acrobat.exe) under Program Files." }
$acro = $candidates | Select-Object -First 1

while (Get-Process -Name 'Acrobat', 'AcroRd32' -ErrorAction SilentlyContinue) {
    Read-Host "Please close Adobe Acrobat (all windows), then press Enter" | Out-Null
}

$plugDir = Join-Path $acro.Dir 'plug_ins'
New-Item -ItemType Directory -Force -Path $plugDir | Out-Null
$api = Join-Path $src "$($acro.Arch)\Tickmarks.api"
Copy-Item $api -Destination (Join-Path $plugDir 'Tickmarks.api') -Force
Unblock-File (Join-Path $plugDir 'Tickmarks.api') -ErrorAction SilentlyContinue
Write-Host "  installed $($acro.Arch) plug-in to $plugDir"

# Remove the earlier script-based Tickmarks for every user profile on this PC, so it doesn't show twice.
Get-ChildItem 'C:\Users' -Directory -ErrorAction SilentlyContinue | ForEach-Object {
    $roam = Join-Path $_.FullName 'AppData\Roaming\Adobe\Acrobat'
    foreach ($old in @('Privileged\DC\JavaScripts\tickmarks.js', 'DC\Stamps\Tickmarks.pdf',
                       'DC\Stamps\Tickmarks Small.pdf', 'DC\Stamps\Tickmarks Large.pdf')) {
        $p = Join-Path $roam $old
        if (Test-Path $p) { Remove-Item $p -Force; Write-Host "  removed old script version: $p" }
    }
}

Write-Host ""
Write-Host "Done. Start Acrobat and open All tools > Tickmarks." -ForegroundColor Green
