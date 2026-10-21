"""Create an independent v2.1.3 package from a matching, unmodified build.
This tool never publishes or launches an executable.
"""
import argparse
import json
from pathlib import Path
import subprocess
import zipfile
import sys
sys.dont_write_bytecode = True
from pe_reference import digest, parse, require, verify

ROOT = Path(__file__).resolve().parents[2]
OLD_VERSION, NEW_VERSION = b"2.1.2", b"2.1.3"
OLD_STAMP = b"Oct 21 2026 00:14:33 ART UTC-3"
NEW_STAMP = b"Oct 21 2026 15:52:49 ART UTC-3"
ZIP_NAME = "Crash-Forge-Racing-Windows.zip"

def package(input_exe, output, strip):
    manifest = json.loads((ROOT/"release_source_manifest.json").read_text(encoding="utf-8"))
    input_exe, output, strip = input_exe.resolve(), output.resolve(), strip.resolve()
    require(input_exe.is_file() and strip.is_file(), "Build input or strip tool is missing")
    require(not output.exists(), "Output must be a new directory: "+str(output))
    require(digest(strip.read_bytes()) == manifest["toolchain"]["files"]["bin/strip.exe"]["sha256"],
            "strip.exe differs from the verified toolchain")
    original = input_exe.read_bytes()
    verify(original, manifest["pe_reference"]["unpatched"])
    for target, entry in manifest["release_resources"].items():
        require(digest((ROOT/entry["source"]).read_bytes()) == entry["sha256"],
                "Release resource differs: "+target)
    output.mkdir(parents=True)
    game = output/"CrashForgeRacing.exe"
    subprocess.run([str(strip),"--strip-debug","-o",str(game),str(input_exe)],
                   check=True, timeout=120)
    stripped = game.read_bytes()
    verify(stripped, manifest["pe_reference"]["stripped_unpatched"])
    sec = parse(stripped)["sections"][".rdata"]
    raw = sec["data"]
    require(raw.count(OLD_VERSION) == 6 and raw.count(NEW_VERSION) == 0,
            "Expected exactly six original version literals in .rdata")
    require(raw.count(OLD_STAMP) == 1 and raw.count(NEW_STAMP) == 0,
            "Expected one original fixed-width menu timestamp")
    patched_raw = raw.replace(OLD_VERSION, NEW_VERSION).replace(OLD_STAMP, NEW_STAMP)
    require(len(patched_raw) == len(raw), "Metadata replacement changed size")
    restored = patched_raw.replace(NEW_VERSION, OLD_VERSION).replace(NEW_STAMP, OLD_STAMP)
    require(restored == raw, "Metadata replacement is not reversible")
    start = sec["offset"]
    patched = stripped[:start]+patched_raw+stripped[start+len(raw):]
    require(len(patched) == len(stripped), "PE file size changed during metadata patch")
    verify(patched, manifest["pe_reference"]["rc3"])
    game.write_bytes(patched)
    require(input_exe.read_bytes() == original, "Input executable unexpectedly changed")
    for target, entry in manifest["release_resources"].items():
        path = output/target
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes((ROOT/entry["source"]).read_bytes())
    names = sorted(["CrashForgeRacing.exe", *manifest["release_resources"]])
    require(names == manifest["rc3"]["files"] and len(names) == 15, "Release allowlist differs")
    archive = output/ZIP_NAME
    with zipfile.ZipFile(archive,"w",zipfile.ZIP_DEFLATED,compresslevel=8) as z:
        for name in names:
            info = zipfile.ZipInfo(name, (2026,10,21,15,52,48))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            z.writestr(info, (output/name).read_bytes(), compresslevel=8)
    with zipfile.ZipFile(archive) as z:
        require(z.testzip() is None and sorted(z.namelist()) == names, "ZIP verification failed")
        for name in names:
            require(z.read(name) == (output/name).read_bytes(), "ZIP member mismatch: "+name)
    checksum = digest(archive.read_bytes())
    (output/(ZIP_NAME+".sha256")).write_text(checksum+"  "+ZIP_NAME+"\n", encoding="ascii")
    report = dict(status="RECONSTRUCTED_PACKAGE_VERIFIED", input_sha256=digest(original),
                  executable_sha256=digest(patched), zip_sha256=checksum,
                  zip_bytes=archive.stat().st_size, file_count=15,
                  strip_debug=True, version_literals_changed=6, menu_timestamps_changed=1,
                  runtime_code_and_data_match_rc3=True, original_input_unchanged=True,
                  scope="Program code/data and packaged resources correspond to approved RC3. "
                        "PE build/debug metadata and ZIP encoding may differ; complete original "
                        "EXE or ZIP SHA256 reproduction is not claimed.")
    (output/"REPRODUCTION_RESULT.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(report,indent=2), flush=True)
    return report

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="Fresh ctr_native.exe from the reproduction build")
    parser.add_argument("--output", type=Path, required=True, help="New isolated output directory")
    parser.add_argument("--strip", type=Path, default=Path(r"C:\msys64\mingw32\bin\strip.exe"))
    args = parser.parse_args()
    package(args.input, args.output, args.strip)
