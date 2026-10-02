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

The project keeps the familiar Qt desktop workflow while narrowing maintenance around a modern Windows + Xray setup. The priorities are correctness, security, stability, a smaller legacy attack surface, and practical day-to-day usability.

Qv2ray-Z is not intended to restore every historical Qv2ray platform, plugin, protocol, or compatibility mode.

## Platform

**Supported:** Windows 10/11 x64

Qv2ray-Z is maintained and packaged for Windows with an official Xray-core Windows x64 build. macOS is deprecated and is not packaged; Linux is not a supported release target.

## Download and run

Download the current Windows package from the [Releases](https://github.com/Zhehao-w/Qv2ray-Z/releases) page, extract the ZIP, and launch Qv2ray-Z from the extracted folder.

The release package includes:

- Qv2ray-Z
- the required Qt runtime and plugins
- a verified official Xray-core Windows x64 binary
- `geoip.dat`
- `geosite.dat`
- release build metadata

The bundled Xray binary and assets are ready for normal use. Advanced users can still point Qv2ray-Z at custom core or asset paths from Preferences when needed.

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
- compatibility with current official Xray-core releases

Unsupported legacy XTLS modes from the original codebase have been removed from normal configuration generation.

## Import and daily use

Qv2ray-Z keeps the familiar Qv2ray connection workflow while focusing the maintained UI on commonly used paths:

- import modern VLESS share links
- import supported configuration files through the advanced import flow
- connect, disconnect, and switch connections from the main window or tray
- enable the Windows system proxy from the application when desired
- use simplified routing presets or advanced custom routing rules
- manage subscriptions with the retained subscription workflow

The maintained application UI is English-only.

## Desktop improvements

Qv2ray-Z keeps the desktop workflow intentionally compact while modernizing the parts that matter for daily use:

- simplified routing presets: **Global Proxy**, **Bypass Mainland China**, **Direct**, and **Custom**
- improved Mainland China routing behavior
- flat connection list with optional Grouped view
- clear separation between connection **Name** and internal Xray **Outbound Tag**
- modernized Qt5 visual styling
- reduced legacy and obsolete UI paths
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

## Compatibility and scope

Qv2ray-Z is intentionally Windows-focused and conservative about new features. Current maintenance emphasizes stable behavior with modern official Xray-core, removal of retired or unreachable legacy paths, and UI cleanup without changing proven protocol behavior.

New protocol support is not a current project goal. Legacy features that are no longer supported by modern Xray-core or by the maintained Windows product may be unavailable even if they existed in historical Qv2ray releases.

## Based on Qv2ray

Original project: [Qv2ray/Qv2ray](https://github.com/Qv2ray/Qv2ray)

Qv2ray-Z continues from the upstream Qv2ray 2.7.0 codebase. Original copyright notices, attribution, project history, and third-party notices are retained.

Qv2ray-Z remains licensed under the [GNU General Public License version 3](LICENSE).
