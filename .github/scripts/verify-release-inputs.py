#!/usr/bin/env python3

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST_PATH = ROOT / ".github" / "release-dependencies.json"
WORKFLOW_DIR = ROOT / ".github" / "workflows"
XRAY_INSTALLER = ROOT / ".github" / "scripts" / "install-pinned-xray-windows.ps1"

HEX64 = re.compile(r"^[0-9a-f]{64}$")
SHA_REF = re.compile(r"^[0-9a-f]{40}$")
USES = re.compile(r"^\s*uses:\s*([^@\s]+)@([^\s#]+)", re.MULTILINE)

QT_INTERNAL_ACTION = "jurplel/install-qt-action/action@48d3ad6db93f3627c8ee7a0454bc6f3744f7e730"
SETUP_PYTHON_ACTION = "actions/setup-python@ece7cb06caefa5fff74198d8649806c4678c61a1"


def fail(message: str) -> None:
    raise SystemExit(f"release input verification failed: {message}")


manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
if manifest.get("schema_version") != 1:
    fail("unsupported release dependency manifest schema")

xray = manifest.get("xray", {})
if not re.fullmatch(r"v\d+\.\d+\.\d+", xray.get("version", "")):
    fail("Xray version must be an explicit release tag")
for platform in ("linux-x64", "windows-x64"):
    asset = xray.get("assets", {}).get(platform, {})
    if not asset.get("name"):
        fail(f"missing Xray asset name for {platform}")
    if not HEX64.fullmatch(asset.get("sha256", "")):
        fail(f"missing or invalid Xray SHA256 for {platform}")

legacy = manifest.get("qv2ray_deps", {})
if not isinstance(legacy.get("release_id"), int) or legacy["release_id"] <= 0:
    fail("Qv2ray-deps release_id must be pinned")
if legacy.get("release_tag") != "release":
    fail("Qv2ray-deps release tag must remain explicit")
required_legacy_assets = {
    "curl-x64-windows.7z",
    "grpc-x64-windows.7z",
    "openssl-x64-windows.7z",
}
if set(legacy.get("assets", {})) != required_legacy_assets:
    fail("Qv2ray-deps Windows x64 asset set changed without updating verification")
for name, asset in legacy["assets"].items():
    if not isinstance(asset.get("asset_id"), int) or asset["asset_id"] <= 0:
        fail(f"missing immutable GitHub asset id for {name}")
    if not isinstance(asset.get("size"), int) or asset["size"] <= 0:
        fail(f"missing pinned asset size for {name}")

for workflow in sorted(WORKFLOW_DIR.glob("*.yml")):
    text = workflow.read_text(encoding="utf-8")
    for action, ref in USES.findall(text):
        if action.startswith("./"):
            continue
        if not SHA_REF.fullmatch(ref):
            fail(f"{workflow.name}: action {action}@{ref} is not pinned to a 40-character commit SHA")

installer_text = XRAY_INSTALLER.read_text(encoding="utf-8")
for marker in ("release-dependencies.json", "Get-FileHash", "Xray checksum mismatch"):
    if marker not in installer_text:
        fail(f"shared Windows Xray installer is missing required verification marker: {marker}")

for name in ("data-safety-hardening.yml", "windows-release.yml", "windows-vless-vision-package.yml"):
    text = (WORKFLOW_DIR / name).read_text(encoding="utf-8")
    if "jurplel/install-qt-action@" in text:
        fail(f"{name}: outer install-qt-action composite contains nested floating action references; use the pinned internal action")
    for marker in (
        QT_INTERNAL_ACTION,
        SETUP_PYTHON_ACTION,
        "python-version: '3.13.3'",
        "aqtversion: '==3.3.0'",
        "py7zrversion: '==1.0.0'",
    ):
        if marker not in text:
            fail(f"{name}: pinned Qt installer chain is missing {marker}")

for name in ("windows-release.yml", "windows-vless-vision-package.yml"):
    text = (WORKFLOW_DIR / name).read_text(encoding="utf-8")
    if "releases/latest" in text or "releases download latest" in text:
        fail(f"{name}: release packaging must not resolve latest dependencies")
    if "install-pinned-xray-windows.ps1" not in text:
        fail(f"{name}: shared pinned Xray installer is not used")

setup_libs = (ROOT / "libs" / "setup-libs.sh").read_text(encoding="utf-8")
if "releases/latest" in setup_libs:
    fail("setup-libs.sh still resolves the latest Qv2ray-deps release")
for marker in ("release-dependencies.json", "release_id", "asset_id", "Downloaded size mismatch"):
    if marker not in setup_libs:
        fail(f"setup-libs.sh is missing required pinned dependency verification marker: {marker}")

version = (ROOT / "makespec" / "VERSION").read_text(encoding="utf-8").strip()
suffix = (ROOT / "makespec" / "VERSIONSUFFIX").read_text(encoding="utf-8").strip()
if not re.fullmatch(r"\d+\.\d+\.\d+-z\d+", version + suffix):
    fail("source release version does not match the supported z-series format")

print("release input verification passed")
