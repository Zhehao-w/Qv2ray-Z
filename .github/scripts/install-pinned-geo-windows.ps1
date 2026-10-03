param(
    [string]$Destination = $(if ($env:RUNNER_TEMP) { Join-Path $env:RUNNER_TEMP 'geo' } else { Join-Path ([System.IO.Path]::GetTempPath()) 'qv2ray-geo' }),
    [string]$DependenciesFile = (Join-Path $PSScriptRoot '..\release-dependencies.json')
)

$ErrorActionPreference = 'Stop'

function Set-WorkflowEnvironment([string]$Name, [string]$Value) {
    Set-Item -Path "Env:$Name" -Value $Value
    if ($env:GITHUB_ENV) {
        "$Name=$Value" | Out-File $env:GITHUB_ENV -Append -Encoding utf8
    }
}

$manifest = Get-Content $DependenciesFile -Raw | ConvertFrom-Json
if ($manifest.schema_version -ne 2) {
    throw 'Pinned dependency metadata does not use the expected schema version.'
}

$rules = $manifest.geo.rules
$cnPrivate = $manifest.geo.cn_private
$assets = @(
    [pscustomobject]@{
        Repository = $rules.repository
        Version = $rules.version
        Name = 'geoip.dat'
        Sha256 = $rules.assets.'geoip.dat'.sha256
    },
    [pscustomobject]@{
        Repository = $rules.repository
        Version = $rules.version
        Name = 'geosite.dat'
        Sha256 = $rules.assets.'geosite.dat'.sha256
    },
    [pscustomobject]@{
        Repository = $cnPrivate.repository
        Version = $cnPrivate.version
        Name = $cnPrivate.asset.name
        Sha256 = $cnPrivate.asset.sha256
    }
)

foreach ($asset in $assets) {
    if ($asset.Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') {
        throw "Pinned Geo repository is invalid: $($asset.Repository)"
    }
    if ($asset.Version -notmatch '^\d{12}$') {
        throw "Pinned Geo release identifier is invalid: $($asset.Version)"
    }
    if (-not $asset.Name -or $asset.Name -match '[\\/]') {
        throw "Pinned Geo asset name is invalid: $($asset.Name)"
    }
    if ($asset.Sha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw "Pinned Geo SHA256 is invalid for $($asset.Name)"
    }
}

$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("qv2ray-geo-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $tempDir | Out-Null

try {
    foreach ($asset in $assets) {
        $target = Join-Path $tempDir $asset.Name
        $url = "https://github.com/$($asset.Repository)/releases/download/$($asset.Version)/$($asset.Name)"
        Write-Host "Downloading pinned Geo asset $($asset.Repository)@$($asset.Version)/$($asset.Name)"
        Invoke-WebRequest -Uri $url -OutFile $target

        $actualHash = (Get-FileHash $target -Algorithm SHA256).Hash.ToLowerInvariant()
        $expectedHash = $asset.Sha256.ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            throw "Geo checksum mismatch for $($asset.Name): expected $expectedHash, got $actualHash"
        }
        Write-Host "Verified $($asset.Name) SHA256: $actualHash"
    }

    New-Item -ItemType Directory -Force $Destination | Out-Null
    foreach ($asset in $assets) {
        Copy-Item (Join-Path $tempDir $asset.Name) (Join-Path $Destination $asset.Name) -Force
    }
}
finally {
    Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}

$resolvedDestination = (Resolve-Path $Destination).Path
Set-WorkflowEnvironment 'GEO_DIR' $resolvedDestination
Set-WorkflowEnvironment 'GEO_RULES_SOURCE' $rules.repository
Set-WorkflowEnvironment 'GEO_RULES_VERSION' $rules.version
Set-WorkflowEnvironment 'GEOIP_ASSET' 'geoip.dat'
Set-WorkflowEnvironment 'GEOIP_SHA256' $rules.assets.'geoip.dat'.sha256.ToLowerInvariant()
Set-WorkflowEnvironment 'GEOSITE_ASSET' 'geosite.dat'
Set-WorkflowEnvironment 'GEOSITE_SHA256' $rules.assets.'geosite.dat'.sha256.ToLowerInvariant()
Set-WorkflowEnvironment 'GEO_CN_PRIVATE_SOURCE' $cnPrivate.repository
Set-WorkflowEnvironment 'GEO_CN_PRIVATE_VERSION' $cnPrivate.version
Set-WorkflowEnvironment 'GEOIP_CN_PRIVATE_ASSET' $cnPrivate.asset.name
Set-WorkflowEnvironment 'GEOIP_CN_PRIVATE_SHA256' $cnPrivate.asset.sha256.ToLowerInvariant()

Write-Host "Verified pinned Geo routing assets in $resolvedDestination"