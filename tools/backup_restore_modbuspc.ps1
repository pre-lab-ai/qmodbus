param(
    [ValidateSet("backup", "restore")]
    [string]$Mode,
    [string]$ArchivePath,
    [string]$DataDirectory = ""
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($DataDirectory)) {
    $DataDirectory = Join-Path $env:LOCALAPPDATA "Foxconn\ModbusPC"
}
if (!(Test-Path $DataDirectory)) { throw "Data directory does not exist: $DataDirectory" }
if ([string]::IsNullOrWhiteSpace($ArchivePath)) { throw "ArchivePath is required" }

if ($Mode -eq "backup") {
    $parent = Split-Path -Parent (Resolve-Path -LiteralPath $ArchivePath -ErrorAction SilentlyContinue)
    if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    if (Test-Path $ArchivePath) { Remove-Item -LiteralPath $ArchivePath -Force }
    Compress-Archive -Path (Join-Path $DataDirectory "*") -DestinationPath $ArchivePath -CompressionLevel Optimal
    Write-Output "Backup created: $ArchivePath"
    exit 0
}

if (!(Test-Path $ArchivePath)) { throw "Backup archive does not exist: $ArchivePath" }
$restoreRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ModbusPC-restore-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $restoreRoot | Out-Null
Expand-Archive -LiteralPath $ArchivePath -DestinationPath $restoreRoot -Force
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$oldData = "$DataDirectory.backup-$stamp"
Move-Item -LiteralPath $DataDirectory -Destination $oldData
New-Item -ItemType Directory -Force -Path $DataDirectory | Out-Null
Copy-Item -LiteralPath (Join-Path $restoreRoot "*") -Destination $DataDirectory -Recurse -Force
Remove-Item -LiteralPath $restoreRoot -Recurse -Force
Write-Output "Data restored to: $DataDirectory"
Write-Output "Previous data preserved at: $oldData"
