<p align="center">
  <img src="assets/icons/qv2ray.256.png" width="128" alt="Qv2ray-Z icon">
</p>

<h1 align="center">Qv2ray-Z</h1>

<p align="center">
  A focused Windows continuation of Qv2ray 2.7.0 for modern Xray-core.
</p>

<p align="center">
  <a href="https://github.com/Zhehao-w/Qv2ray-Z/releases/latest"><img src="https://img.shields.io/github/v/release/Zhehao-w/Qv2ray-Z?display_name=tag&label=release" alt="Latest release"></a>
  <a href="https://github.com/Zhehao-w/Qv2ray-Z/actions/workflows/windows-vless-vision-package.yml"><img src="https://github.com/Zhehao-w/Qv2ray-Z/actions/workflows/windows-vless-vision-package.yml/badge.svg?branch=dev" alt="Windows package"></a>
  <a href="https://github.com/Zhehao-w/Qv2ray-Z/actions/workflows/vless-vision-validation.yml"><img src="https://github.com/Zhehao-w/Qv2ray-Z/actions/workflows/vless-vision-validation.yml/badge.svg?branch=dev" alt="VLESS Vision compatibility"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPLv3-blue.svg" alt="GPLv3 license"></a>
</p>

<p align="center">
  <a href="https://github.com/Zhehao-w/Qv2ray-Z/releases/latest"><strong>Download the latest Windows release</strong></a>
</p>

## What is Qv2ray-Z?

Qv2ray-Z is a personal continuation of the discontinued [Qv2ray](https://github.com/Qv2ray/Qv2ray) desktop client, based on the upstream Qv2ray 2.7.0 codebase.

The project keeps the familiar Qt desktop workflow while narrowing maintenance around a modern Windows + Xray setup. The priorities are correctness, security, stability, reproducible releases, a smaller legacy attack surface, and practical day-to-day usability.

Qv2ray-Z is not intended to restore every historical Qv2ray platform, plugin, protocol, or compatibility mode.

## Platform

**Supported:** Windows 10/11 x64

The authoritative build environment is:

- Qt 5.15.2
- MSVC 14.2
- Windows Server 2022
- official Xray-core Windows x64 releases

Linux CI, where retained, is diagnostic only. macOS is deprecated and is not built or packaged.

## Modern Xray support

The maintained configuration paths currently include:

- VLESS over TCP / RAW
- TLS
- REALITY
- `xtls-rprx-vision`
- `xtls-rprx-vision-udp443`
- REALITY ML-DSA-65 verification (`pqv` / `mldsa65Verify`)
- modern VLESS share-link import and export
- bundled Xray executable discovery
- validation against current official Xray-core releases

Unsupported legacy XTLS modes from the original codebase have been removed from normal configuration generation.

## Desktop improvements

Qv2ray-Z keeps the desktop workflow intentionally compact while modernizing the parts that matter for daily use:

- simplified routing presets: **Global Proxy**, **Bypass Mainland China**, **Direct**, and **Custom**
- improved Mainland China routing behavior
- flat connection list with optional Grouped view
- clear separation between connection **Name** and internal Xray **Outbound Tag**
- modernized Qt5 visual styling
- reduced legacy and obsolete UI paths
- English-only maintained UI
- quieter connection and proxy notifications for fresh configurations
- existing realtime speed chart and traffic statistics retained

## Routing

The simplified routing presets are built on the existing Qv2ray routing model.

For example, **Bypass Mainland China** sends:

- private / LAN traffic → Direct
- `geoip:cn` → Direct
- `geosite:cn` → Direct
- other traffic → Proxy

Explicit custom routing rules keep higher priority, and advanced routing controls remain available when needed.

## Releases

Windows releases are produced from versioned Git tags.

A release tag must use the form:

```text
v<major>.<minor>.<patch>-z<number>
```

Before tagging, the source version and release notes must already exist in `dev`:

- `makespec/VERSION`
- `makespec/VERSIONSUFFIX`
- `docs/release-notes/<version>.md`

A typical Codespaces release flow is:

```bash
git switch dev
git pull --ff-only
git tag -a v2.7.0-z3 -m "Qv2ray-Z v2.7.0-z3"
git push origin v2.7.0-z3
```

Pushing the tag automatically starts `.github/workflows/windows-release.yml`. The workflow verifies that:

- the tag format is valid
- the tag version matches the source version
- matching release notes exist
- the tagged commit belongs to `dev` history
- the Windows package builds successfully
- TLS Vision and REALITY Vision fixtures validate with the pinned official Xray-core
- retired native Qv2ray plugin DLLs and translation bundles are not packaged
- packaged Qv2ray-Z and Xray binaries pass smoke checks

If validation succeeds, GitHub Actions creates the versioned ZIP, writes its SHA256 checksum, and publishes the GitHub Release automatically.

## Release contents

The Windows x64 release package includes:

- Qv2ray-Z
- Qt runtime and required plugins
- verified official Xray-core Windows x64 binary
- `geoip.dat`
- `geosite.dat`
- release build metadata

Downloads and checksums are available from the [Releases](https://github.com/Zhehao-w/Qv2ray-Z/releases) page.

## Build and validation

The primary maintained workflows are:

- `.github/workflows/windows-vless-vision-package.yml` — authoritative Windows package build and smoke test
- `.github/workflows/windows-release.yml` — tag-driven release packaging and publishing
- `.github/workflows/vless-vision-validation.yml` — VLESS / TLS / REALITY / Vision compatibility checks
- `.github/workflows/data-safety-hardening.yml` — application and regression validation

The repository also contains Xray fixtures and regression coverage under:

- `.github/xray-fixtures/`
- `test/`

## Maintenance scope

The repository is intentionally Windows-focused and conservative about new features.

Current maintenance priorities are:

1. correctness and security
2. stable compatibility with modern official Xray-core
3. reproducible Windows builds and releases
4. removal of retired or unreachable legacy code
5. focused UI cleanup without changing proven protocol behavior

New protocol support is not a current project goal.

## Based on Qv2ray

Original project: [Qv2ray/Qv2ray](https://github.com/Qv2ray/Qv2ray)

Qv2ray-Z continues from the upstream Qv2ray 2.7.0 codebase. Original copyright notices, attribution, project history, and third-party notices are retained.

Qv2ray-Z remains licensed under the [GNU General Public License version 3](LICENSE).
