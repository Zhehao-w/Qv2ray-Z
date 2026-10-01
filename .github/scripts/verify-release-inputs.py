#!/usr/bin/env python3

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST_PATH = ROOT / ".github" / "release-dependencies.json"
WORKFLOW_DIR = ROOT / ".github" / "workflows"

HEX64 = re.compile(r"^[0-9a-f]{64}$")
SHA_REF = re.compile(r"^[0-9a-f]{40}$")
USES = re.compile(r"^\s*uses:\s*([^@\s]+)@([^\s#]+)", re.MULTILINE)


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

for name in ("windows-release.yml", "windows-vless-vision-package.yml"):
    text = (WORKFLOW_DIR / name).read_text(encoding="utf-8")
    if "releases/latest" in text or "releases download latest" in text:
        fail(f"{name}: release packaging must not resolve latest dependencies")
    if "release-dependencies.json" not in text:
        fail(f"{name}: release dependency manifest is not consumed")

setup_libs = (ROOT / "libs" / "setup-libs.sh").read_text(encoding="utf-8")
if "releases/latest" in setup_libs:
    fail("setup-libs.sh still resolves the latest Qv2ray-deps release")
if "release-dependencies.json" not in setup_libs:
    fail("setup-libs.sh does not consume the pinned dependency manifest")

version = (ROOT / "makespec" / "VERSION").read_text(encoding="utf-8").strip()
suffix = (ROOT / "makespec" / "VERSIONSUFFIX").read_text(encoding="utf-8").strip()
if not re.fullmatch(r"\d+\.\d+\.\d+-z\d+", version + suffix):
    fail("source release version does not match the supported z-series format")

print("release input verification passed")
