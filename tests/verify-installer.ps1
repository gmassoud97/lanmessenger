param(
  [Parameter(Mandatory = $true)][string]$Installer,
  [Parameter(Mandatory = $true)][string]$PortableZip
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Installer)) { throw "Installer missing: $Installer" }
if (-not (Test-Path $PortableZip)) { throw "Portable package missing: $PortableZip" }

$version = (Get-Item $Installer).VersionInfo
if ($version.ProductName -ne 'MBC LAN Messenger') {
  throw "Unexpected installer ProductName: $($version.ProductName)"
}
if ($version.FileVersion -notmatch '^1\.2\.39.*Revision 2026-09-15') {
  throw "Unexpected installer FileVersion: $($version.FileVersion)"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($PortableZip)
try {
  $names = @($zip.Entries | ForEach-Object FullName)
  foreach ($required in @('lmc.exe', 'libcrypto-1_1-x64.dll', 'libssl-1_1-x64.dll', 'lmc.rcc')) {
    if ($names -notcontains $required) { throw "Portable package is missing $required" }
  }
} finally {
  $zip.Dispose()
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
