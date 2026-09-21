param(
  [Parameter(Mandatory = $true)][string]$Installer
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Installer)) { throw "Installer missing: $Installer" }

$version = (Get-Item $Installer).VersionInfo
if ($version.ProductName -ne 'MBC LAN Messenger') {
  throw "Unexpected installer ProductName: $($version.ProductName)"
}
if ($version.FileVersion -notmatch '^1\.2\.39.*Revision 2026-09-15') {
  throw "Unexpected installer FileVersion: $($version.FileVersion)"
}

$installDir = Join-Path $env:RUNNER_TEMP 'lmc-installer-smoke'
if (Test-Path $installDir) { Remove-Item $installDir -Recurse -Force }

$process = Start-Process -FilePath $Installer -ArgumentList '/S', "/D=$installDir" -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Installer exited with code $($process.ExitCode)" }

foreach ($required in @('lmc.exe', 'libcrypto-1_1-x64.dll', 'libssl-1_1-x64.dll', 'uninstall.exe')) {
  if (-not (Test-Path (Join-Path $installDir $required))) {
    throw "Installed application is missing $required"
  }
}

$appVersion = (Get-Item (Join-Path $installDir 'lmc.exe')).VersionInfo
if ($appVersion.FileVersion -notmatch '^1\.2\.39') {
  throw "Unexpected lmc.exe version: $($appVersion.FileVersion)"
}

$uninstaller = Join-Path $installDir 'uninstall.exe'
$process = Start-Process -FilePath $uninstaller -ArgumentList '/S' -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Uninstaller exited with code $($process.ExitCode)" }

Write-Host 'Windows installer smoke test passed.'
