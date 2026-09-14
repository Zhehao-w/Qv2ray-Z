# Qv2ray-Z

[![Windows VLESS Vision package](https://github.com/Zhehao-w/Qv2ray-Z/actions/workflows/windows-vless-vision-package.yml/badge.svg?branch=dev)](https://github.com/Zhehao-w/Qv2ray-Z/actions/workflows/windows-vless-vision-package.yml)
[![VLESS Vision compatibility](https://github.com/Zhehao-w/Qv2ray-Z/actions/workflows/vless-vision-validation.yml/badge.svg?branch=dev)](https://github.com/Zhehao-w/Qv2ray-Z/actions/workflows/vless-vision-validation.yml)
[![GitHub release](https://img.shields.io/github/v/release/Zhehao-w/Qv2ray-Z?display_name=tag)](https://github.com/Zhehao-w/Qv2ray-Z/releases)
[![License: GPLv3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)

Qv2ray-Z is a community fork of the discontinued [Qv2ray](https://github.com/Qv2ray/Qv2ray) project. Upstream Qv2ray ended at version 2.7.0; Qv2ray-Z continues from that codebase and modernizes the client for current official Xray-core releases.

Windows x64 is currently the primary validated release platform. Linux and macOS build support remains in the source tree, but Qv2ray-Z does not currently claim release-tested packages for those platforms.

## Supported Xray features

- VLESS over TCP with TLS
- VLESS TCP REALITY
- `xtls-rprx-vision` and `xtls-rprx-vision-udp443` flows
- REALITY ML-DSA-65 verification (`pqv`)
- Modern VLESS share-link import and export
- A current official Xray-core bundled in the Windows package
- Automatic discovery of the bundled Xray executable

Legacy XTLS modes that are unsupported by current Xray-core have been removed and are no longer exposed by Qv2ray-Z.

## Releases

Release builds and checksums are published on the [Qv2ray-Z releases page](https://github.com/Zhehao-w/Qv2ray-Z/releases). The Windows ZIP contains Qv2ray-Z, the required Qt runtime and plugins, and an official Xray-core build with its GeoIP and GeoSite data files.

## Building and testing

Qv2ray-Z retains Qv2ray's CMake build. The validated Windows recipe uses Qt 5.15.2, MSVC 14.2, and the dependency setup in `libs/setup-libs.sh`; see the [Windows package workflow](.github/workflows/windows-vless-vision-package.yml) for the exact steps. Protocol serialization tests and Xray compatibility fixtures live under `test/` and `.github/xray-fixtures/`.

## Based on Qv2ray

Original project: <https://github.com/Qv2ray/Qv2ray>

Qv2ray-Z continues from the upstream Qv2ray 2.7.0 codebase. Original copyright notices and project history are retained. Qv2ray-Z is free software licensed under the [GNU General Public License version 3](LICENSE), and its builds continue to include the existing third-party attribution.

## Contributing

Bug reports and focused contributions are welcome in the [Qv2ray-Z repository](https://github.com/Zhehao-w/Qv2ray-Z). Please include the Qv2ray-Z version, operating system, Qt version, and relevant logs when reporting a problem.
