# Qv2ray-Z

[![Windows VLESS Vision package](https://github.com/Zhehao-w/Qv2ray-Z/actions/workflows/windows-vless-vision-package.yml/badge.svg?branch=dev)](https://github.com/Zhehao-w/Qv2ray-Z/actions/workflows/windows-vless-vision-package.yml)
[![VLESS Vision compatibility](https://github.com/Zhehao-w/Qv2ray-Z/actions/workflows/vless-vision-validation.yml/badge.svg?branch=dev)](https://github.com/Zhehao-w/Qv2ray-Z/actions/workflows/vless-vision-validation.yml)
[![GitHub release](https://img.shields.io/github/v/release/Zhehao-w/Qv2ray-Z?display_name=tag)](https://github.com/Zhehao-w/Qv2ray-Z/releases)
[![License: GPLv3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)

Qv2ray-Z is a personal continuation of the discontinued [Qv2ray](https://github.com/Qv2ray/Qv2ray) desktop client, based on the upstream Qv2ray 2.7.0 codebase.

The project is maintained primarily for personal Windows use, with a focus on keeping the original Qt desktop experience compatible with modern official Xray-core releases.

## Current stable release

**Qv2ray-Z v2.7.0-z2**

Windows x64 is the primary supported and tested platform.

Release packages include:

- Qv2ray-Z
- Qt runtime and required plugins
- Current official Xray-core Windows x64 build
- `geoip.dat`
- `geosite.dat`

See the [Releases](../../releases) page for packaged builds and SHA256 checksums.

## Modern Xray support

Qv2ray-Z currently supports the modern Xray configurations used by this project:

- VLESS over TCP / RAW
- TLS
- REALITY
- `xtls-rprx-vision`
- `xtls-rprx-vision-udp443`
- REALITY ML-DSA-65 verification (`pqv` / `mldsa65Verify`)
- Modern VLESS share-link import and export
- Bundled Xray executable discovery
- Validation against current official Xray-core releases

Unsupported legacy XTLS modes from the original Qv2ray codebase have been removed from normal configuration generation.

## Desktop improvements

Compared with the original Qv2ray 2.7.0 interface, Qv2ray-Z includes several usability improvements:

- Simplified routing modes:
  - Global Proxy
  - Bypass Mainland China
  - Direct
  - Custom
- Improved Mainland China routing behavior
- Flat connection list with optional Grouped view
- Clear separation between connection **Name** and internal Xray **Outbound Tag**
- Cleaner preferences
- Reduced legacy / obsolete options
- Quiet connection and proxy notifications by default for fresh configurations
- Existing realtime speed chart and traffic statistics retained

## Routing

The simplified routing presets are built on top of the existing Qv2ray routing model.

For example, **Bypass Mainland China** uses:

- private / LAN traffic → Direct
- `geoip:cn` → Direct
- `geosite:cn` → Direct
- other traffic → Proxy

Explicit custom routing rules retain higher priority.

Advanced routing controls remain available when needed.

## Private repository behavior

This repository is maintained for personal use.

The built-in GitHub release update checker is disabled by default because the repository and releases are private.

Release builds are produced through GitHub Actions and downloaded manually from the private Releases page.

## Building

The primary validated Windows build uses:

- Windows Server 2022
- Qt 5.15.2
- MSVC 14.2
- CMake / Ninja

The exact build and packaging process is defined in:

- `.github/workflows/windows-vless-vision-package.yml`
- `.github/workflows/windows-release.yml`

The Windows package automatically downloads and bundles the latest official stable Xray-core release available at build time.

## Validation

The repository includes automated checks for:

- TLS + Vision
- REALITY + Vision
- REALITY ML-DSA-65 verification
- VLESS share-link serialization
- Current Xray-core configuration compatibility
- Windows Qt 5.15.2 packaging

See:

- `.github/workflows/vless-vision-validation.yml`
- `.github/xray-fixtures/`
- `test/`

## Based on Qv2ray

Original project:

https://github.com/Qv2ray/Qv2ray

Qv2ray-Z continues from the upstream Qv2ray 2.7.0 codebase.

Original copyright notices, attribution, project history, and third-party notices are retained.

Qv2ray-Z remains licensed under the [GNU General Public License version 3](LICENSE).
