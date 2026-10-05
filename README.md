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

The project keeps the familiar Qt desktop workflow while narrowing maintenance around a modern Windows + Xray setup. The priorities are correctness, security, configuration safety, stability, a smaller legacy attack surface, and practical day-to-day usability.

Qv2ray-Z is not intended to restore every historical Qv2ray platform, plugin, protocol, or compatibility mode. The primary day-to-day target is modern VLESS with TLS or REALITY and Vision, including REALITY ML-DSA-65 verification where configured.

## Development status

The latest published stable release is **v3.0.0**. The `dev` branch currently identifies itself as **3.1.0** and is being prepared for the next Windows release.

There is no v3.1.0 release tag yet. v3.1.0 remains blocked until the latest `dev` commit completes authoritative Windows validation and the packaged UI receives a final manual sanity check. See the current [v3.1.0 release notes](docs/release-notes/3.1.0.md) for the release scope.

## Platform

**Supported release target:** Windows 10/11 x64

Qv2ray-Z is maintained and packaged for Windows with Qt 5.15.2, MSVC 14.2, and an official Xray-core Windows x64 build. macOS is deprecated and is not packaged; Linux CI is diagnostic only and is not an authoritative release gate.

## Download and run

Download the current Windows package from the [Releases](https://github.com/Zhehao-w/Qv2ray-Z/releases) page, extract the ZIP, and launch Qv2ray-Z from the extracted folder.

The release package includes:

- Qv2ray-Z
- the required Qt runtime and plugins
- a verified official Xray-core Windows x64 binary
- `geoip.dat`
- `geosite.dat`
- `geoip-only-cn-private.dat`
- `release-manifest.json` with source, Xray, Geo, Qt, and toolchain metadata

The bundled Xray binary and assets are ready for normal use. Advanced users can still point Qv2ray-Z at custom core or asset paths from Preferences when needed.

## Modern Xray support

The maintained configuration paths currently include:

- VLESS
- modern VLESS `encryption` values preserved verbatim through supported import/export and runtime generation
- TLS
- REALITY
- `xtls-rprx-vision`
- `xtls-rprx-vision-udp443`
- REALITY ML-DSA-65 verification (`pqv` / `mldsa65Verify`)
- TCP
- WebSocket
- mKCP using current Xray-supported fields
- gRPC
- XHTTP
- FinalMask
- modern VLESS share-link import and export
- preservation of unknown / duplicate / raw VLESS query metadata across supported round-trips
- bundled Xray executable discovery
- compatibility with the pinned official Xray-core release

### Configuration safety

Qv2ray-Z keeps editing and import/export conservative when a configuration contains data the maintained UI does not own:

- unmanaged connection-root and outbound JSON is preserved during supported graphical edits
- unknown settings, extra server/user entries, and fields not owned by supported single-server editors are preserved instead of being flattened away
- unknown and nested transport fields are preserved instead of being dropped by typed editor round-trips
- unsupported stream-security metadata and opaque TLS certificate fields are preserved rather than silently normalized away
- TLS certificate pins are validated at the final editor and kernel-start boundaries so unsafe preserved JSON cannot bypass the guard
- unknown, duplicate, and raw VLESS query metadata is preserved across supported share-link round-trips
- routing/DNS editor saves preserve unmanaged auxiliary JSON and structured DNS data
- invalid or unreadable routing storage fails closed for dependent connections instead of silently falling back to default routing
- Qv2ray-internal preservation metadata is stripped before the final Xray runtime configuration is generated
- cleanup does not automatically translate retired features into superficially similar modern features

For example, legacy HTTP transport is not migrated to XHTTP, and legacy mKCP Header camouflage is not migrated to FinalMask.

### Disabled or removed legacy surfaces

The maintained product intentionally does not expose or generate several historical Qv2ray/Xray paths that are retired, unsupported by current Xray-core, or no longer useful for this Windows-focused client:

- legacy QUIC transport
- legacy HTTP / H2 / H3 stream transport
- legacy mKCP Header camouflage
- legacy mKCP Seed
- legacy SSD (`ssd://`) share-link import
- obsolete MTProto outbound editor
- inactive Android and historical Debian / Snap / RPM packaging paths
- unreachable inbound placeholder editors that were never part of the maintained build

Some old persisted model fields are intentionally retained for backward compatibility and lossless loading. Their presence in the data model does **not** mean they can be newly created, exported, or passed to modern Xray at runtime. Unsupported runtime fields are rejected or stripped at the final runtime boundary rather than silently migrated to unrelated modern features.

In particular, FinalMask is a current Xray feature and is not treated as legacy transport/header camouflage.

## Import and daily use

Qv2ray-Z keeps the familiar Qv2ray connection workflow while focusing the maintained UI on commonly used paths:

- import modern VLESS share links
- import supported configuration files through the advanced import flow
- connect, disconnect, and switch connections from the main window or tray
- use **System Proxy on Connect** as the persistent automatic proxy preference
- use the tray **Enable System Proxy** / **Disable System Proxy** actions as immediate manual commands
- use the synchronized **Bypass CN Mainland** tray toggle or Preferences setting for the same persisted routing preference
- use simplified routing presets or advanced custom routing rules
- manage subscriptions with the retained subscription workflow

The maintained application UI is English-only.

## Desktop improvements

Qv2ray-Z keeps the desktop workflow intentionally compact while modernizing the parts that matter for daily use:

- simplified routing presets: **Global Proxy**, **Bypass Mainland China**, **Direct**, and **Custom**
- improved Mainland China routing behavior and synchronized tray/Preferences state
- flat connection list with optional Grouped view
- clear separation between connection **Name** and internal Xray **Outbound Tag**
- first-launch sizing that gives the Connection pane more room while preserving saved user geometry
- connection rows sized to avoid clipping underscores and descenders
- stable Connect / Disconnect button width so connection state changes do not shift the header layout
- consistent full-surface hover feedback for neutral actions while retaining semantic Connect / Disconnect colors
- **System Proxy on Connect** surfaced under Preferences → General without changing the existing automatic proxy lifecycle semantics
- modernized Qt5 visual styling
- reduced legacy and obsolete UI paths
- quieter connection and proxy notifications for fresh configurations
- existing realtime speed chart and traffic statistics retained

## Routing

The simplified routing presets are built on the existing Qv2ray routing model.

For example, **Bypass Mainland China** sends:

- private / LAN traffic → Direct
- explicit custom Block / Proxy / Direct rules keep priority
- `geoip:cn` → Direct
- `geosite:cn` → Direct
- other traffic → Proxy

The tray **Bypass CN Mainland** action and the Preferences checkbox use the same persisted `bypassCN` state. Changing the tray toggle while connected reuses the existing connection restart path so the routing change takes effect.

## Release integrity

The authoritative release target is Windows x64 with Qt 5.15.2 and MSVC 14.2. The Windows release pipeline builds and runs the maintained regression targets, installs and deploys the application, verifies pinned release inputs, downloads and verifies the pinned official Xray binary and Geo assets, validates routing fixtures with Xray, and smoke-tests the packaged output.

The heavyweight `routing_ui_safety` regression is part of explicit Windows release validation. A manually dispatched authoritative Windows workflow enables it directly; rerunning the same workflow at a later attempt also enables it. Linux remains diagnostic-only.

Pinned release metadata covers the official Xray binary, `geoip.dat`, `geosite.dat`, `geoip-only-cn-private.dat`, and the retained Windows dependency archives. The generated `release-manifest.json` records the source commit and verified release inputs used for the package.

## Compatibility and scope

Qv2ray-Z is intentionally Windows-focused and conservative about new features. Current maintenance emphasizes stable behavior with modern official Xray-core, preservation of imported and subscription-created configuration data, removal of retired or unreachable legacy paths, and focused UI cleanup without broad rewrites of proven runtime behavior.

Existing configuration data is handled conservatively: cleanup work avoids destructive automatic migration and avoids silently rewriting unrelated or unknown configuration fields.

New protocol support is not a current project goal. Legacy features that are no longer supported by modern Xray-core or by the maintained Windows product may be unavailable even if they existed in historical Qv2ray releases.

## Based on Qv2ray

Original project: [Qv2ray/Qv2ray](https://github.com/Qv2ray/Qv2ray)

Qv2ray-Z continues from the upstream Qv2ray 2.7.0 codebase. Original copyright notices, attribution, project history, and applicable third-party notices are retained.

Qv2ray-Z remains licensed under the [GNU General Public License version 3](LICENSE).
