$ErrorActionPreference = 'Stop'

$manifestPath = Join-Path $PSScriptRoot '..\release-dependencies.json'
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$tag = $manifest.xray.version
$asset = $manifest.xray.assets.'windows-x64'
if (-not $tag -or -not $asset.name -or -not $asset.sha256) {
    throw 'Pinned Xray Windows dependency metadata is incomplete.'
}
if ($tag -notmatch '^v\d+\.\d+\.\d+$' -or $asset.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
    throw 'Pinned Xray Windows dependency metadata has an invalid version or SHA256.'
}

$xrayDir = Join-Path $env:RUNNER_TEMP 'xray'
New-Item -ItemType Directory -Force $xrayDir | Out-Null
$archive = Join-Path $xrayDir $asset.name
$url = "https://github.com/XTLS/Xray-core/releases/download/$tag/$($asset.name)"
Invoke-WebRequest -Uri $url -OutFile $archive

$actualHash = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
$expectedHash = $asset.sha256.ToLowerInvariant()
if ($actualHash -ne $expectedHash) {
    throw "Xray checksum mismatch: expected $expectedHash, got $actualHash"
}

$binDir = Join-Path $xrayDir 'bin'
Expand-Archive $archive $binDir -Force
$xrayBin = Join-Path $binDir 'xray.exe'
if (-not (Test-Path $xrayBin -PathType Leaf)) {
    throw "Pinned Xray archive does not contain xray.exe: $($asset.name)"
}

$versionOutput = & $xrayBin version 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) {
    throw 'Pinned xray.exe could not report its version.'
}
$expectedVersion = $tag -replace '^v', ''
if ($versionOutput -notmatch [regex]::Escape($expectedVersion)) {
    throw "Pinned xray.exe did not report manifest version $tag"
}

"XRAY_BIN=$xrayBin" | Out-File $env:GITHUB_ENV -Append -Encoding utf8
"XRAY_VERSION=$tag" | Out-File $env:GITHUB_ENV -Append -Encoding utf8
"XRAY_ASSET=$($asset.name)" | Out-File $env:GITHUB_ENV -Append -Encoding utf8
"XRAY_SHA256=$expectedHash" | Out-File $env:GITHUB_ENV -Append -Encoding utf8
Write-Host "Verified pinned Xray release $tag ($($asset.name), SHA256 $expectedHash)"
