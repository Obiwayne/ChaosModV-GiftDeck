# Assembles the GiftDeck build of Chaos Mod V the same way the official release zip is laid out:
#
#   dist\ChaosMod.asi                     (this build)
#   dist\chaosmod\...                     (unchanged files from the official 2.2 release: config app, voting proxy,
#                                          Twitch overlay, natives_def.lua, README, licenses) + GiftDeck notes
#   dist\chaosmod\version.txt             "ChaosMod version <version>-giftdeck.<n>" (read by GiftDeck's Game Packs)
#
# and release\chaosmod-<version>.zip (same content, for a GitHub release; asset name matches ^chaosmod.*\.zip$).
#
# The C# config app / voting proxy / overlay are not modified in this fork, so they are taken from the official
# upstream 2.2 release zip (downloaded once into scripts\.cache and checked against its SHA-256).
param(
    [Parameter(Mandatory = $true)][string]$Asi
)
$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$cache = Join-Path $PSScriptRoot '.cache'
$dist = Join-Path $root 'dist'
$release = Join-Path $root 'release'

$baseUrl = 'https://github.com/gta-chaos-mod/ChaosModV/releases/download/2.2/chaosmod.zip'
$baseSha = '047D66EC922CF9519E2CD942F9581BC5B0DBC0AFE036A08BEEB32364AE28DC7D'
$baseZip = Join-Path $cache 'chaosmod-2.2-official.zip'

$modVersion = (Get-Content (Join-Path $root 'version.txt') -Raw).Trim()
$gdBuild = [regex]::Match((Get-Content (Join-Path $root 'ChaosMod\Components\GiftDeckLink.h') -Raw),
    '#define GIFTDECK_BUILD_VERSION "([^"]+)"').Groups[1].Value
$version = "$modVersion-giftdeck.$gdBuild"

New-Item -ItemType Directory -Force $cache | Out-Null
if (-not (Test-Path $baseZip) -or (Get-FileHash $baseZip -Algorithm SHA256).Hash -ne $baseSha) {
    Write-Host "Downloading official Chaos Mod 2.2 release (unchanged support files)..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $baseUrl -OutFile $baseZip -UseBasicParsing
    if ((Get-FileHash $baseZip -Algorithm SHA256).Hash -ne $baseSha) { throw "SHA-256 mismatch for $baseZip" }
}

if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force $dist | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::ExtractToDirectory($baseZip, $dist)

# The official zip has a README.txt next to ChaosMod.asi; keep it out of the game's root folder
if (Test-Path (Join-Path $dist 'README.txt')) {
    Move-Item (Join-Path $dist 'README.txt') (Join-Path $dist 'chaosmod\README\README (Chaos Mod).txt') -Force
}

Copy-Item $Asi (Join-Path $dist 'ChaosMod.asi') -Force

$utf8 = New-Object System.Text.UTF8Encoding($false)
$built = [DateTime]::UtcNow.ToString('u').Replace('Z', ' UTC')
[IO.File]::WriteAllText((Join-Path $dist 'chaosmod\version.txt'),
    "ChaosMod version $version`r`n`r`nBuilt on $built`r`nGiftDeck build of Chaos Mod V (GPL-3), based on gta-chaos-mod/ChaosModV $modVersion (tag 2.2)`r`n", $utf8)

Copy-Item (Join-Path $root 'LICENSE') (Join-Path $dist 'chaosmod\Licenses\LICENSE.ChaosModV.txt') -Force
Copy-Item (Join-Path $root 'GIFTDECK.md') (Join-Path $dist 'chaosmod\README\giftdeck.txt') -Force

# Release zip: ChaosMod.asi + chaosmod\ at the top, like the official chaosmod.zip
New-Item -ItemType Directory -Force $release | Out-Null
$zip = Join-Path $release "chaosmod-$version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
[IO.Compression.ZipFile]::CreateFromDirectory($dist, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)

Write-Host "dist:    $dist"
Write-Host "release: $zip"
Write-Host "version: $version"
