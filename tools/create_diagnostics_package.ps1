param(
    [string]$OutputDirectory = "",
    [string]$DataDirectory = ""
)

$ErrorActionPreference = "Stop"
$projectDirectory = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectDirectory "..\build_qt6\diagnostics"
}
if ([string]::IsNullOrWhiteSpace($DataDirectory)) {
    $DataDirectory = Join-Path $env:LOCALAPPDATA "Foxconn\ModbusPC"
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path $OutputDirectory).Path
$stamp = (Get-Date).ToUniversalTime().ToString("yyyyMMdd-HHmmssZ")
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("ModbusPC-diagnostics-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$manifest = Join-Path $projectDirectory "..\build_qt6\deploy_step8\release_manifest.json"
if (Test-Path $manifest) { Copy-Item -Force $manifest (Join-Path $stage "release_manifest.json") }
Copy-Item -Force (Join-Path $projectDirectory "data\point_table.json") (Join-Path $stage "point_table.json")
Copy-Item -Force (Join-Path $projectDirectory "data\settings.template.ini") (Join-Path $stage "settings.template.ini")

$summary = [ordered]@{
    generated_utc = $stamp
    data_directory = $DataDirectory
    trace_logging = $false
    database_present = Test-Path (Join-Path $DataDirectory "acquisition.sqlite")
    database_size_bytes = if (Test-Path (Join-Path $DataDirectory "acquisition.sqlite")) { (Get-Item (Join-Path $DataDirectory "acquisition.sqlite")).Length } else { 0 }
    database_wal_present = Test-Path (Join-Path $DataDirectory "acquisition.sqlite-wal")
    database_shm_present = Test-Path (Join-Path $DataDirectory "acquisition.sqlite-shm")
    point_table_sha256 = (Get-FileHash (Join-Path $projectDirectory "data\point_table.json") -Algorithm SHA256).Hash.ToLowerInvariant()
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $stage "diagnostic_summary.json")
if (Test-Path $DataDirectory) {
    Get-ChildItem -LiteralPath $DataDirectory -File -ErrorAction SilentlyContinue |
        Select-Object Name,Length,LastWriteTimeUtc |
        ConvertTo-Json -Depth 3 |
        Set-Content -Encoding UTF8 (Join-Path $stage "data_file_inventory.json")
}

$zipPath = Join-Path $OutputDirectory ("ModbusPC-diagnostics-" + $stamp + ".zip")
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zipPath -CompressionLevel Optimal
Remove-Item -LiteralPath $stage -Recurse -Force
Write-Output "Diagnostics package: $zipPath"
