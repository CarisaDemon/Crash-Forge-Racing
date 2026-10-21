"""Build the verified Windows x86 source in a new directory, with at most two jobs."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
sys.dont_write_bytecode = True
from pe_reference import digest, require, verify
from package_release import package

ROOT = Path(__file__).resolve().parents[2]

def run(command, env, cwd, log, timeout):
    with log.open("w",encoding="utf-8") as stream:
        proc = subprocess.Popen(command,cwd=cwd,env=env,stdout=stream,stderr=subprocess.STDOUT)
        print("Running", log.name, "PID", proc.pid, flush=True)
        try:
            status = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill","/PID",str(proc.pid),"/T","/F"],stdout=stream,stderr=stream)
            raise
    require(status == 0, "Command failed; see "+str(log))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True,help="New isolated directory; must not already exist")
    parser.add_argument("--toolchain",type=Path,default=Path(r"C:\msys64\mingw32\bin"))
    parser.add_argument("--preflight-only",action="store_true",help="Validate source and existing tools, without copying or compiling")
    args = parser.parse_args()
    require(os.name == "nt", "This exact reconstruction recipe targets Windows/MSYS2 MinGW32")
    manifest = json.loads((ROOT/"release_source_manifest.json").read_text(encoding="utf-8"))
    out, tc = args.output.resolve(), args.toolchain.resolve()
    require(not out.exists(), "Refusing an existing output directory")
    for relative, entry in manifest["source_files"].items():
        file = ROOT/relative
        require(file.is_file() and digest(file.read_bytes()) == entry["sha256"],
                "Source checksum mismatch: "+relative)
    for relative, entry in manifest["toolchain"]["files"].items():
        file = tc.parent/relative
        require(file.is_file() and digest(file.read_bytes()) == entry["sha256"],
                "Verified toolchain file missing/different: "+relative)
    env = dict(os.environ)
    env["PATH"] = str(tc)+os.pathsep+env.get("PATH","")
    env["SOURCE_DATE_EPOCH"] = "1792541673"
    probe = subprocess.run([str(tc/"i686-w64-mingw32-gcc.exe"),"-E","-P","-x","c","-"],
        input='__DATE__ " " __TIME__\n',text=True,capture_output=True,env=env,timeout=15)
    require(probe.returncode == 0 and probe.stdout.strip() == '"Oct 21 2026" " " "00:14:33"',
            "Compiler metadata macros differ from the verified build")
    print("Preflight passed:",len(manifest["source_files"]),"source files; toolchain hashes verified",flush=True)
    if args.preflight_only:
        return
    started = time.time()
    out.mkdir(parents=True)
    src, build, temp = out/"source", out/"build", out/"temp"
    src.mkdir()
    temp.mkdir()
    for relative in manifest["source_files"]:
        target = src/relative
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/relative,target)
        require(digest(target.read_bytes()) == manifest["source_files"][relative]["sha256"],
                "File changed during source copy: "+relative)
    # Keep CMake's original "unknown-dirty" ID even when this package is in a Git checkout.
    # The isolated copy has no .git. Git must not discover a parent repository.
    env["GIT_CEILING_DIRECTORIES"] = str(out)
    env.pop("GIT_DIR",None)
    env.pop("GIT_WORK_TREE",None)
    env["TEMP"], env["TMP"] = str(temp), str(temp)
    options = [
        "-DCMAKE_BUILD_TYPE=Release","-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON","-DCTR_LTO=ON","-DCTR_CPU_NATIVE=OFF",
        "-DCTR_PGO_MODE=OFF","-DCTR_CSEQ_LIBERATION=ON","-DCTR_WIN64=OFF",
        "-DCTR_PRACTICE_FASTSTART=OFF","-DCTR_CSEQ_TEST_BUILD=OFF",
        "-DCTR_CNK_TORNADO_QA=OFF","-DCTR_CNK_PHYSICS_PREVIEW_QA=OFF",
        "-DCTR_CNK_NATIVE_BRIDGE_QA=OFF","-DCTR_PC_RENDER_QA=OFF"]
    configure = [str(tc/"cmake.exe"),"-S",str(src),"-B",str(build),"-G","MinGW Makefiles",
        "-DCMAKE_C_COMPILER="+(tc/"i686-w64-mingw32-gcc.exe").as_posix(),
        "-DCMAKE_CXX_COMPILER="+(tc/"i686-w64-mingw32-g++.exe").as_posix(),
        "-DCMAKE_MAKE_PROGRAM="+(tc/"mingw32-make.exe").as_posix(),
        "-DCMAKE_C_LINKER_LAUNCHER="+Path(sys.executable).as_posix()+";"+
            (ROOT/"tools/release213/limit_lto.py").as_posix(),*options]
    env["SOURCE_DATE_EPOCH"] = "1792552161"
    run(configure,env,out,out/"configure.log",900)
    flags = (build/"CMakeFiles/ctr_native.dir/flags.make").read_text(errors="replace")
    require("unknown-dirty-20261021030921" in flags,"CMake build ID differs")
    command = [str(tc/"cmake.exe"),"--build",str(build),"--target","ctr_native","--parallel","2"]
    env["SOURCE_DATE_EPOCH"] = "1792541673"
    run(command,env,out,out/"build.log",1800)
    for relative, entry in manifest["source_files"].items():
        require(digest((src/relative).read_bytes()) == entry["sha256"],"Build changed a source file: "+relative)
        require(digest((ROOT/relative).read_bytes()) == entry["sha256"],"Original source changed during build: "+relative)
    verify((build/"ctr_native.exe").read_bytes(),manifest["pe_reference"]["unpatched"])
    report = package(build/"ctr_native.exe",out/"release",tc/"strip.exe")
    record = dict(configure_command=configure,build_command=command,
                  configure_source_date_epoch=1792552161,compile_source_date_epoch=1792541673,
                  seconds=round(time.time()-started,2),max_build_jobs=2,max_lto_jobs=2,
                  runtime_correspondence_verified=True,release=report)
    (out/"BUILD_RESULT.json").write_text(json.dumps(record,indent=2)+"\n",encoding="utf-8")
    print("Completed:",out/"release"/"Crash-Forge-Racing-Windows.zip",flush=True)

if __name__ == "__main__":
    main()
