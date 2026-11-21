"""Package Ocarina release builds and validate Android signing and alignment."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request
import xml.etree.ElementTree as ET
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile


ROOT = Path(__file__).resolve().parent.parent
PROJECT = "Ocarina-of-Resonance"
BUNDLE = "Ocarina of Resonance.vst3"
APP_ID = "com.blinkzer0.ocarina"
SDK_TOOLS = "36.0.0"
BUNDLETOOL_VERSION = "1.18.3"
BUNDLETOOL_SHA256 = "a099cfa1543f55593bc2ed16a70a7c67fe54b1747bb7301f37fdfd6d91028e29"
VST3_METADATA = "Contents/Resources/moduleinfo.json"
VST3_BINARY = "Contents/x86_64-win/Ocarina of Resonance.vst3"
VST3_NOTICES = "Contents/Resources/licenses/"


def require(condition, explanation):
    if not condition:
        raise ValueError(explanation)


def version(tag=None):
    value = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    require(re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", value),
            "VERSION needs a major.minor.patch version.")
    require(tag is None or tag == f"v{value}", f"Expected tag v{value}, received {tag}.")
    return value


def asset_names():
    release = f"v{version()}"
    return {
        "windows": f"{PROJECT}-Windows-{release}.vst3.zip",
        "web": f"{PROJECT}-Web-{release}.zip",
        "apk": f"{PROJECT}-Android-{release}.apk",
        "aab": f"{PROJECT}-Android-{release}.aab",
        "debug": f"{PROJECT}-Android-{release}-debug.apk",
    }


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run(*command):
    completed = subprocess.run(command, check=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    return completed.stdout


def tool(name):
    default_sdk = Path(os.environ["LOCALAPPDATA"]) / "Android/Sdk" if os.name == "nt" else None
    sdk = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT") or default_sdk
    require(sdk, "Set ANDROID_HOME to the Android SDK directory.")
    suffix = {"apksigner": ".bat"}.get(name, ".exe") if os.name == "nt" else ""
    path = Path(sdk) / "build-tools" / SDK_TOOLS / (name + suffix)
    require(path.is_file(), f"Install Android build-tools;{SDK_TOOLS}. Missing {path.name}.")
    return path


def check_panel(read, root=""):
    index = read(root + "index.html").decode("utf-8")
    require(f"main.js?v={version()}" in index, "The instrument panel is built from a different VERSION.")
    for name in ("main.js", "presets.js", "sequence.js", "midi-file.js", "parameters.json", "ocarina.webp", "style.css"):
        require(read(root + "assets/" + name), f"Missing instrument panel asset {name}.")


def check_windows_bundle(read):
    # Steinberg's module metadata can contain trailing commas.
    metadata = read(VST3_METADATA).decode("utf-8-sig")
    versions = re.findall(r'^\s*"Version"\s*:\s*"([^"]+)"', metadata, re.M)
    require(versions and all(value == version() for value in versions), "The VST3 version differs from VERSION.")
    require(read(VST3_BINARY).startswith(b"MZ"), "The Windows x64 VST3 binary is missing or invalid.")


def check_web(read):
    check_panel(read)
    require(read("assets/ocarina.wasm").startswith(b"\0asm"), "The web bundle needs the compiled Wasm module.")
    require(read("assets/worklet.js"), "The web bundle needs the audio worklet.")
    read(".nojekyll")


def notice_files(platform):
    files = sorted((ROOT / "licenses" / platform).glob("*.txt"))
    require(files, f"Missing source notices for {platform}.")
    return files


def check_notices(read, platform, prefix):
    for file in notice_files(platform):
        require(read(prefix + file.name) == file.read_bytes(), f"Missing or mismatched notice {file.name}.")


def archive(platform, source, output):
    read = lambda name: (source / name).read_bytes()
    added_notices = []
    if platform == "windows":
        require(source.name == BUNDLE, f"Select the {BUNDLE} directory.")
        check_windows_bundle(read)
        for file in notice_files(platform):
            relative = VST3_NOTICES + file.name
            existing = source / relative
            if existing.exists():
                require(read(relative) == file.read_bytes(), f"Mismatched archive notice {file.name}.")
            else:
                added_notices.append((file, f"{BUNDLE}/{relative}"))
    else:
        check_web(read)
    output.mkdir(parents=True, exist_ok=True)
    path = output / asset_names()[platform]
    with ZipFile(path, "w", compression=ZIP_DEFLATED, compresslevel=9) as release:
        for file in sorted(source.rglob("*")):
            if file.is_file():
                require(not file.is_symlink(), f"Package assets must be regular files. Found {file.name}.")
                relative = file.relative_to(source)
                release.write(file, Path(BUNDLE) / relative if platform == "windows" else relative)
        for file, name in added_notices:
            release.write(file, name)
    print(path)


def fetch_bundletool(directory):
    directory.mkdir(parents=True, exist_ok=True)
    name = f"bundletool-all-{BUNDLETOOL_VERSION}.jar"
    destination = directory / name
    if not destination.exists():
        url = f"https://github.com/google/bundletool/releases/download/{BUNDLETOOL_VERSION}/{name}"
        with tempfile.NamedTemporaryFile(dir=directory, delete=False) as temp:
            downloaded = Path(temp.name)
        try:
            with urllib.request.urlopen(url, timeout=60) as response, downloaded.open("wb") as target:
                shutil.copyfileobj(response, target)
            require(sha256(downloaded) == BUNDLETOOL_SHA256, "The bundletool download failed SHA-256 verification.")
            downloaded.replace(destination)
        finally:
            downloaded.unlink(missing_ok=True)
    check_bundletool(destination)
    return destination.resolve()


def check_bundletool(path):
    require(sha256(path) == BUNDLETOOL_SHA256, f"Use the verified bundletool {BUNDLETOOL_VERSION} release.")


def validate_native(archive, prefix, apk):
    libraries = [member for member in archive.infolist()
                 if member.filename.startswith(prefix) and member.filename.endswith(".so")]
    require(any(member.filename == prefix + "arm64-v8a/libocarina_android.so" for member in libraries),
            "No ARM64 Ocarina audio library was packaged.")
    for member in libraries:
        name = member.filename.removeprefix(prefix)
        require(name.startswith("arm64-v8a/") and name.count("/") == 1, f"Unexpected library ABI in {name}.")
        require(not apk or member.compress_type == ZIP_STORED, f"{name} must be uncompressed in the APK.")
        data = archive.read(member)
        require(data[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<H", data, 18)[0] == 183,
                f"{name} is not a 64-bit ARM ELF library.")
        table = struct.unpack_from("<Q", data, 32)[0]
        stride, count = struct.unpack_from("<HH", data, 54)
        loads = 0
        for offset in range(table, table + stride * count, stride):
            kind, _, file_offset, address, _, _, _, alignment = struct.unpack_from("<IIQQQQQQ", data, offset)
            if kind == 1:  # PT_LOAD
                require(alignment >= 16384 and (address - file_offset) % 16384 == 0,
                        f"{name} has a load segment without 16 KB alignment.")
                loads += 1
        require(loads > 0, f"{name} has no loadable segments.")


def validate_apk(path, debug):
    report = run(tool("apksigner"), "verify", "--print-certs", path)
    signers = re.findall(r"^Signer #\d+ certificate SHA-256 digest: ([0-9a-fA-F]{64})\s*$", report, re.M)
    require(len(signers) == 1, "The APK must have one valid signing certificate.")
    run(tool("zipalign"), "-c", "-P", "16", "4", path)
    badging = run(tool("aapt2"), "dump", "badging", path)
    identity = re.search(r"^package: name='([^']+)' versionCode='([1-9][0-9]*)' versionName='([^']+)'", badging, re.M)
    require(identity and identity[1] == APP_ID + (".debug" if debug else "") and identity[3] == version(),
            "Unexpected Android package name or version.")
    require(("application-debuggable" in badging) == debug, "The APK has the wrong debuggable setting.")
    with ZipFile(path) as apk:
        check_panel(apk.read, "assets/")
        check_notices(apk.read, "android", "assets/licenses/")
        validate_native(apk, "lib/", True)
    return signers[0].lower(), identity[2]


def validate_aab(path, bundletool, signer, version_code):
    check_bundletool(bundletool)
    run("java", ROOT / "scripts/VerifyBundle.java", path, signer)
    run("java", "-jar", bundletool, "validate", f"--bundle={path}")
    config = run("java", "-jar", bundletool, "dump", "config", f"--bundle={path}")
    require("PAGE_ALIGNMENT_16K" in config, "The AAB does not request 16 KB alignment.")
    xml = run("java", "-jar", bundletool, "dump", "manifest", f"--bundle={path}", "--module=base")
    manifest = ET.fromstring(xml)
    android = "{http://schemas.android.com/apk/res/android}"
    require(manifest.get("package") == APP_ID and manifest.get(android + "versionName") == version()
            and manifest.get(android + "versionCode") == version_code, "The AAB identity differs from the APK.")
    require(manifest.find("application").get(android + "debuggable") != "true", "The AAB is debuggable.")
    with ZipFile(path) as bundle:
        check_panel(bundle.read, "base/assets/")
        check_notices(bundle.read, "android", "base/assets/licenses/")
        validate_native(bundle, "base/lib/", False)


def package_android(args):
    fingerprint, version_code = validate_apk(args.apk, args.kind == "debug")
    if args.kind == "release":
        if args.certificate_sha256:
            require(fingerprint == args.certificate_sha256.replace(":", "").lower(),
                    "The APK signer differs from the expected release certificate.")
        require(args.aab and args.bundletool, "Release packaging requires both the AAB and bundletool.")
        validate_aab(args.aab, args.bundletool, fingerprint, version_code)
    print(f"Verified Android {args.kind} {version()} ({version_code}), ARM64, 16 KB alignment, signer SHA-256 {fingerprint}")
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        names = asset_names()
        if args.kind == "debug":
            print(shutil.copyfile(args.apk, args.output / names["debug"]))
        else:
            print(shutil.copyfile(args.apk, args.output / names["apk"]))
            print(shutil.copyfile(args.aab, args.output / names["aab"]))


def checksums(directory, rehearsal):
    names = asset_names()
    with ZipFile(directory / names["windows"]) as windows:
        read_windows = lambda path: windows.read(f"{BUNDLE}/{path}")
        check_windows_bundle(read_windows)
        check_notices(read_windows, "windows", VST3_NOTICES)
        require(windows.testzip() is None, "Corrupt Windows archive.")
    with ZipFile(directory / names["web"]) as web:
        check_web(web.read)
        require(web.testzip() is None, "Corrupt web archive.")
    kinds = ("windows", "web") if rehearsal else ("windows", "apk", "aab", "web")
    lines = [f"{sha256(directory / names[kind])}  {names[kind]}" for kind in kinds]
    path = directory / "SHA256SUMS.txt"
    path.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    print(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("version").add_argument("--tag")
    for platform in ("windows", "web"):
        command = commands.add_parser(platform)
        command.add_argument("source", type=Path)
        command.add_argument("output", type=Path)
    android = commands.add_parser("android")
    android.add_argument("kind", choices=["debug", "release"])
    android.add_argument("--apk", type=Path, required=True)
    android.add_argument("--aab", type=Path)
    android.add_argument("--bundletool", type=Path)
    android.add_argument("--certificate-sha256")
    android.add_argument("--output", type=Path)
    commands.add_parser("bundletool").add_argument("directory", type=Path)
    verify = commands.add_parser("checksums")
    verify.add_argument("directory", type=Path)
    verify.add_argument("--rehearsal", action="store_true")
    args = parser.parse_args()
    try:
        if args.command == "version":
            print(version(args.tag))
        elif args.command in ("windows", "web"):
            archive(args.command, args.source, args.output)
        elif args.command == "android":
            package_android(args)
        elif args.command == "bundletool":
            print(fetch_bundletool(args.directory))
        else:
            checksums(args.directory, args.rehearsal)
    except subprocess.CalledProcessError as error:
        print(error.stdout, file=sys.stderr)
        return 1
    except (OSError, ValueError, KeyError, struct.error) as error:
        print(f"Packaging failed. {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
