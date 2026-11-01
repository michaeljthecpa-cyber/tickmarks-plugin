<# Tickmarks plug-in for Adobe Acrobat - uninstaller. #>
$ErrorActionPreference = 'Stop'
# Re-launch with admin rights if needed (Acrobat's plug_ins folder is under Program Files).
$me = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $me.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell -Verb RunAs -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-NoExit', '-File', "`"$PSCommandPath`"")
    exit
}
while (Get-Process -Name 'Acrobat', 'AcroRd32' -ErrorAction SilentlyContinue) {
    Read-Host "Please close Adobe Acrobat (all windows), then press Enter" | Out-Null
}
foreach ($d in @("$env:ProgramFiles\Adobe\Acrobat DC\Acrobat", "${env:ProgramFiles(x86)}\Adobe\Acrobat DC\Acrobat",
                 "$env:ProgramFiles\Adobe\Acrobat 2020\Acrobat", "${env:ProgramFiles(x86)}\Adobe\Acrobat 2020\Acrobat")) {
    $p = Join-Path $d 'plug_ins\Tickmarks.api'
    if ($d -and (Test-Path $p)) { Remove-Item $p -Force; Write-Host "  removed $p" }
}
Write-Host "Tickmarks removed. Marks already placed in PDFs stay as they are." -ForegroundColor Green
