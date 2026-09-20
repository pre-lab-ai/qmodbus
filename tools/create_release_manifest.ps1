param(
    [string]$DeploymentDirectory = "",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$projectDirectory = (Resolve-Path (Join-Path $PSScriptRoot ".." )).Path
if ([string]::IsNullOrWhiteSpace($DeploymentDirectory)) {
    $DeploymentDirectory = Join-Path $projectDirectory "..\build_qt6\deploy_qt6"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectDirectory "..\build_qt6\release_package"
}
$DeploymentDirectory = (Resolve-Path $DeploymentDirectory).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path $OutputDirectory).Path

$exe = Join-Path $DeploymentDirectory "qmodbus.exe"
$pointTable = Join-Path $projectDirectory "data\point_table.json"
$template = Join-Path $projectDirectory "data\settings.template.ini"
if (!(Test-Path $exe)) { throw "Missing deployed executable: $exe" }
if (!(Test-Path $pointTable)) { throw "Missing point table: $pointTable" }

$configDirectory = Join-Path $DeploymentDirectory "config"
New-Item -ItemType Directory -Force -Path $configDirectory | Out-Null
Copy-Item -Force $pointTable (Join-Path $configDirectory "point_table.json")
Copy-Item -Force $template (Join-Path $configDirectory "settings.template.ini")

$manifest = [ordered]@{
    product = "ModbusPC"
    version = "0.1.1"
    build_timestamp_utc = (Get-Date).ToUniversalTime().ToString("o")
    qt = "6.11.2"
    compiler = "MSVC 2022 x64"
    libmodbus = "3.1.1"
    point_table_sha256 = (Get-FileHash $pointTable -Algorithm SHA256).Hash.ToLowerInvariant()
    executable_sha256 = (Get-FileHash $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    database_schema_version = 2
    mutable_data_directory = "%LOCALAPPDATA%\\Foxconn\\ModbusPC"
    database_file = "acquisition.sqlite"
    trace_logging = $false
}
$manifestPath = Join-Path $DeploymentDirectory "release_manifest.json"
$manifest | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $manifestPath
$hashPath = Join-Path $OutputDirectory "ModbusPC-0.1.1-sha256.txt"
(Get-FileHash $exe -Algorithm SHA256).Hash.ToLowerInvariant() + "  qmodbus.exe" | Set-Content -Encoding ASCII $hashPath
$zipPath = Join-Path $OutputDirectory "ModbusPC-0.1.1-win64.zip"
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
Compress-Archive -Path (Join-Path $DeploymentDirectory "*") -DestinationPath $zipPath -CompressionLevel Optimal
Copy-Item -Force $manifestPath (Join-Path $OutputDirectory "release_manifest.json")
Write-Output "Release package: $zipPath"
Write-Output "Manifest: $manifestPath"
