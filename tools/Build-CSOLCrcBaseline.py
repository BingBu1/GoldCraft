"""Build a CRC-OFF control, or relink an existing verified ON/OFF object pair.

Uses Ninja's expanded link command and compile_commands.json. All source stays
in place; original objects, library, DLL and PDB remain read-only. prepare never
compiles; build verifies the saved plan before executing sequentially.
prepare-relink/relink use historical artifacts only and never compile current
product sources. Their receipt must not be described as a current-source build.
"""
import argparse
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
MACRO = "CSOL_OPTIMIZE_HOST_CRC"


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def identity(path):
    path = Path(path).resolve(strict=True)
    before = path.stat()
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    after = path.stat()
    if (before.st_mtime_ns, before.st_size) != (after.st_mtime_ns, after.st_size):
        raise ValueError(f"Input changed while hashing: {path}")
    return dict(path=str(path), bytes=after.st_size, sha256=digest)


def save(path, value):
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def record_link_products(directory, receipt):
    """Anchor successful link outputs before fallible log/PDB interpretation."""
    products = [identity(directory / name) for name in
                ("CSOLSupport.dll", "CSOLSupport.pdb", "CSOLSupport.lib")]
    save(receipt, dict(products=products, recordedUnixNs=time.time_ns(),
                       scope="Successful link outputs; validation still pending"))
    return products


def split_windows(command):
    count = ctypes.c_int()
    split = ctypes.windll.shell32.CommandLineToArgvW
    split.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    split.restype = ctypes.POINTER(ctypes.c_wchar_p)
    values = split(command, ctypes.byref(count))
    if not values:
        raise ctypes.WinError()
    try:
        return [values[i] for i in range(count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(values)


def safe_output(value, fresh=False):
    path = Path(value).absolute()
    for parent in (path, *path.parents):
        if parent.is_symlink() or parent.is_junction():
            raise ValueError(f"Output traverses link: {parent}")
    path = path.resolve()
    if not path.is_relative_to(ROOT / "build") or fresh and path.exists():
        raise ValueError("Expected a fresh output under workspace build")
    return path


def verify(rows):
    changed = [r["path"] for r in rows if identity(r["path"]) != r]
    if changed:
        raise ValueError("Input identities changed: " + repr(changed))


def key(path):
    return str(Path(path).resolve()).casefold()


def prepare(args):
    base = Path(args.base).resolve(strict=True)
    out = safe_output(args.output, fresh=True)
    verification = base.parent / "verification.json"
    verified = read(verification)
    if not verified["passed"] or not verified["crcEnabled"] or verified["refreshEnabled"]:
        raise ValueError("Expected successful CRC-ON, refresh-OFF build receipt")
    seed = read(args.seed_plan)
    environment = seed["environment"]
    verify([environment, seed["compiler"]])
    env = dict(os.environ)
    env.update(read(environment["path"]))
    database = read(base / "compile_commands.json")
    source = ROOT / "native/csol/client/model_crc.cpp"
    target = [r for r in database if key(r["file"]) == key(source)]
    if len(target) != 1:
        raise ValueError("Expected exactly one CRC compilation")
    original_compile = split_windows(target[0]["command"])
    if original_compile.count("-D" + MACRO + "=1") != 1:
        raise ValueError("CRC compile definition is not exactly ON")
    compile_args = []
    original_object = None
    for arg in original_compile:
        if arg == "-D" + MACRO + "=1":
            arg = "-D" + MACRO + "=0"
        elif arg.startswith("/Fo"):
            original_object = (base / arg[3:]).resolve()
            arg = "/Fo" + str(out / "model_crc.cpp.obj")
        elif arg.startswith("/Fd"):
            arg = "/Fd" + str(out / "compile.pdb")
        compile_args.append(arg)
    if original_object is None:
        raise ValueError("Missing original object output")
    # Diagnostic only: actual include audit, no changed optimization/ABI flags.
    compile_args.insert(1, "/showIncludes")
    cache = (base / "CMakeCache.txt").read_text()
    ninja = re.search(r"^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$", cache, re.M).group(1)
    raw_link = subprocess.check_output([ninja, "-C", str(base), "-t", "commands", "-s", "CSOLSupport.dll"], text=True).strip()
    match = re.fullmatch(r'[^\r\n]+? /C "cd \. && (.+) && cd \."', raw_link)
    if not match:
        raise ValueError("Unsupported Ninja wrapper; inspect actual command")
    original_link = split_windows(match[1])
    if original_link[1:3] != ["-E", "vs_link_dll"]:
        raise ValueError("Expected CMake manifest-aware link wrapper")
    link_args, direct_objects, libraries = [], [], []
    libdirs = [base, *(Path(p) for p in env["LIB"].split(";") if p)]

    def library(name):
        for directory in libdirs:
            path = directory / name
            if path.is_file():
                return path.resolve()
        raise ValueError(f"Unresolved library: {name}")

    for arg in original_link:
        if arg.startswith("--intdir="):
            arg = "--intdir=" + str(out / "manifest")
        elif arg.lower().startswith(("/out:", "/implib:", "/pdb:")):
            flag, name = arg.split(":", 1)
            if Path(name).name != name:
                raise ValueError("Unexpected output layout")
            arg = flag + ":" + str(out / name)
        elif arg.lower().endswith(".obj"):
            path = (base / arg).resolve(strict=True)
            direct_objects.append(path)
            arg = str(out / "model_crc.cpp.obj") if path == original_object else str(path)
        elif arg.lower().endswith(".lib"):
            path = library(arg)
            libraries.append(path)
            arg = str(path)
        link_args.append(arg)
    if direct_objects.count(original_object) != 1 or "/threads:1" not in link_args:
        raise ValueError("Expected one replaced object and single-thread link")
    # Keep the original link settings. /verbose only records the libraries read.
    link_args.append("/verbose")
    original_rows = {key(r["path"]): r for r in verified["linkedObjects"]}
    direct_keys = {key(p) for p in direct_objects}
    if not direct_keys <= original_rows.keys() or len(original_rows) != verified["linkedObjectCount"]:
        raise ValueError("Original link object set differs from verified receipt")
    archive_objects = [r for k, r in original_rows.items() if k not in direct_keys]
    archive = library("csol_damage_vgui.lib")
    llvm = Path(seed["compiler"]["path"]).parent
    listing = subprocess.check_output([str(llvm / "llvm-lib.exe"), "/list", str(archive)], text=True).splitlines()
    if {key(base / p) for p in listing if p.strip()} != {key(r["path"]) for r in archive_objects}:
        raise ValueError("Static library member set differs from verified objects")
    # The compiler's /MT default libraries are also registered before linking;
    # /verbose subsequently rejects any unregistered library read.
    libraries += [library(name) for name in ("libcmt.lib", "libcpmt.lib", "oldnames.lib", "libvcruntime.lib", "libucrt.lib")]
    source_records = read(base.parent / "inputs.json")["sources"]
    expected = {key(r["path"]): r for r in [*source_records, *verified["actualDependencies"],
                                           *verified["linkedObjects"], *verified["products"]]}
    # Scan all actual dependencies and every compile database TU. Generated
    # command-line definitions are intentionally excluded from the source scan.
    scan_paths = {Path(r["path"]) for r in verified["actualDependencies"]}
    scan_paths.update(Path(r["file"]) for r in database)
    consumers = [str(p.resolve()) for p in sorted(scan_paths) if MACRO.encode() in p.read_bytes()]
    if consumers != [str(source.resolve())]:
        raise ValueError(f"CRC macro has other consumers: {consumers}")
    verify(list(expected.values()))
    additional = [Path(__file__), ROOT / "tools/Inspect-MetaHookRuntime.py", verification,
                  base.parent / "inputs.json", base / "compile_commands.json", base / "build.ninja",
                  base / "CMakeFiles/rules.ninja", base / "CMakeCache.txt", Path(args.seed_plan),
                  Path(environment["path"]), Path(ninja), Path(original_compile[0]), Path(original_link[0]),
                  llvm / "lld-link.exe", llvm / "llvm-lib.exe", llvm / "llvm-pdbutil.exe", *libraries]
    for arg in original_link:
        if arg.startswith(("--rc=", "--mt=")):
            additional.append(Path(arg.split("=", 1)[1]))
    for path in additional:
        expected[key(path)] = identity(path)
    out.mkdir(parents=True)
    (out / "manifest").mkdir()
    (out / "temp").mkdir()
    plan = dict(base=str(base), output=str(out), preparedUnixNs=time.time_ns(),
                revision=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                environment=environment, originalCompile=original_compile, compile=compile_args,
                originalNinjaLink=raw_link, originalLink=original_link, link=link_args,
                replacedObject=str(original_object), directObjects=[str(p) for p in direct_objects],
                archiveObjects=archive_objects, staticLibrary=identity(archive),
                macroConsumers=consumers, macroScanFiles=len(scan_paths),
                registeredLibraries=[identity(p) for p in sorted(set(libraries))],
                inputs=list(expected.values()), singleJob=True, sourceCopies=False,
                productionChange="CSOL_OPTIMIZE_HOST_CRC=1 to 0 in model_crc.cpp only")
    save(out / "plan.json", plan)
    print(json.dumps(dict(prepared=True, inputs=len(expected), directObjects=len(direct_objects),
                          archiveObjects=len(archive_objects), macroConsumers=consumers)))


def build(args):
    out = safe_output(args.output)
    plan = read(out / "plan.json")
    if plan["output"] != str(out) or (out / "result.json").exists():
        raise ValueError("Plan/output mismatch or result already exists")
    verify(plan["inputs"])
    env = dict(os.environ)
    env.update(read(plan["environment"]["path"]))
    env.update(VSLANG="1033", VERBOSE="1", TMP=str(out / "temp"), TEMP=str(out / "temp"))
    result = dict(passed=False, commands=[], deployed=False, gameplay=False)

    def run(name, command, timeout=300):
        start = time.monotonic()
        done = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=timeout,
                              creationflags=subprocess.CREATE_NO_WINDOW)
        text = done.stdout + done.stderr
        log = out / (name + ".log")
        log.write_text(text, encoding="utf-8")
        result["commands"].append(dict(name=name, argv=command, exit=done.returncode,
                                        seconds=time.monotonic()-start, log=identity(log)))
        if done.returncode:
            raise RuntimeError(f"{name} failed: {log}")
        return text

    try:
        compile_log = run("compile", plan["compile"])
        registered = {key(r["path"]): r for r in plan["inputs"]}
        includes = [Path(p) for p in re.findall(r"^\s*Note: including file:\s*(.+?)\s*$", compile_log, re.M)]
        if not includes or any(key(p) not in registered for p in includes):
            raise RuntimeError("Unregistered or missing actual CRC includes")
        result["actualIncludes"] = [identity(p) for p in sorted(set(includes))]
        link_log = run("link", plan["link"])
        reads = re.findall(r"Reading (.+?\.lib)(?:\(|\r?$)", link_log, re.M | re.I)
        result["actualLibraryReads"] = sorted(set(reads))
        if not reads or any(key(p) not in registered for p in reads):
            raise RuntimeError("Unregistered or missing actual library reads")
        spec = importlib.util.spec_from_file_location("runtime_pe", ROOT / "tools/Inspect-MetaHookRuntime.py")
        audit = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(audit)
        dll, pdb = out / "CSOLSupport.dll", out / "CSOLSupport.pdb"
        pe = audit.PE(dll)
        tool = Path(plan["compile"][0]).parent / "llvm-pdbutil.exe"
        summary = run("pdb", [str(tool), "dump", "-summary", "-files", str(pdb)])
        guid = re.search(r"GUID: \{([^}]+)\}", summary).group(1).lower()
        age = int(re.search(r"Age: (\d+)", summary).group(1))
        if not pe.codeview or (guid, age) != (pe.codeview["guid"], pe.codeview["age"]):
            raise RuntimeError("DLL/PDB mismatch")
        if b"GoldCraftCsolResource001" in pe.data or b"CSOL_CRC32_ProcessBuffer" in pe.data:
            raise RuntimeError("Unexpected refresh interface or optimized CRC hook identifier")
        verify(plan["inputs"])
        result.update(passed=True, inputsUnchanged=True, originalObjectsUnchanged=213,
                      x86=True, pdbGuid=guid, pdbAge=age, crcHookIdentifierAbsent=True,
                      products=[identity(p) for p in (dll, pdb, out / "CSOLSupport.lib", out / "model_crc.cpp.obj")])
    except Exception as error:
        result["failure"] = str(error)
        raise
    finally:
        save(out / "result.json", result)
    print(json.dumps(dict(passed=True, products=result["products"], pdbGuid=guid, pdbAge=age)))


def library_reads(text):
    """Read lld's actual input trace, not its command line or PDB contributors."""
    reads, members = set(), []
    for line in text.splitlines():
        match = re.search(r"(?:^|:\s+)Reading (.+?\.lib)(?:\(([^\r\n]*)\))?\s*$", line, re.I)
        if match:
            path = Path(match[1])
            if path.is_absolute():
                reads.add(str(path.resolve(strict=True)))
            elif match[2] is not None:
                members.append(match[1])
            else:
                raise ValueError("Unresolved actual library open: " + match[1])
    if not reads:
        raise ValueError("Linker did not report any library reads")
    for member in members:
        matches = [p for p in reads if Path(p).name.casefold() == member.casefold()]
        if len(matches) != 1:
            raise ValueError("Archive member lacks one unambiguous absolute library open: " + member)
    return sorted(reads, key=key)


def registered_library_reads(text, registered):
    reads = library_reads(text)
    missing = [p for p in reads if key(p) not in registered]
    if missing:
        raise ValueError("Unregistered actual library reads: " + repr(missing))
    verify([registered[key(p)] for p in reads])
    return reads


def pair_link_signature(command, output, object_path):
    """Normalize only output locations and the one designated CRC object."""
    normalized = []
    for arg in command:
        if arg == str(object_path):
            normalized.append("<CRC-OBJECT>")
        elif arg.startswith("--intdir="):
            if Path(arg.split("=", 1)[1]) != output / "manifest":
                raise ValueError("Unexpected manifest output")
            normalized.append("--intdir=<OUTPUT>/manifest")
        elif arg.lower().startswith(("/out:", "/pdb:", "/implib:")):
            prefix, value = arg.split(":", 1)
            if Path(value).parent != output:
                raise ValueError("Link output escapes variant directory")
            normalized.append(prefix + "<OUTPUT>/" + Path(value).name)
        else:
            normalized.append(arg)
    if normalized.count("<CRC-OBJECT>") != 1:
        raise ValueError("Link must use exactly one designated CRC object")
    return normalized


def check_pair(plan):
    signatures = []
    original_objects = {key(r["path"]): r for r in plan["originalObjects"]}
    if len(original_objects) != 213:
        raise ValueError("Expected the verified 213-object baseline")
    direct = {key(p) for p in plan["directObjects"]}
    archives = {key(r["path"]) for r in plan["archiveObjects"]}
    if direct & archives or direct | archives != original_objects.keys():
        raise ValueError("Direct/archive object partition does not match provenance")
    for variant in plan["variants"]:
        command = variant["argv"]
        output = Path(variant["output"])
        crc = Path(variant["crcObject"]["path"])
        if command[1:3] != ["-E", "vs_link_dll"] or "/threads:1" not in command:
            raise ValueError("Expected the real single-thread CMake link wrapper")
        if "/verbose" not in command:
            raise ValueError("Missing actual linker input trace")
        signatures.append(pair_link_signature(command, output, crc))
        objects = [key(arg) for arg in command if arg.lower().endswith(".obj")]
        expected = (direct - {key(plan["onObject"]["path"])}) | {key(crc)}
        if len(objects) != len(direct) or set(objects) != expected:
            raise ValueError("Unexpected added, removed or duplicated object")
    if len(signatures) != 2 or signatures[0] != signatures[1]:
        raise ValueError("ON/OFF commands differ beyond CRC object and output paths")


def probe_wrapper(cmake, out, environment):
    """Exercise successful output capture with cmake -E echo, never a linker."""
    marker = "CSOL_WRAPPER_OUTPUT_CAPTURE_PROBE"
    command = [str(cmake), "-E", "vs_link_dll", "--msvc-ver=1951",
               "--intdir=" + str(out), "--", str(cmake), "-E", "echo", marker,
               "/out:" + str(out / "not-a-product.dll"), "/manifest:no", "/incremental:no"]
    rows = []
    for verbose in (False, True):
        env = dict(environment)
        env.pop("VERBOSE", None)
        if verbose:
            env["VERBOSE"] = "1"
        done = subprocess.run(command, cwd=out, env=env, capture_output=True,
                              text=True, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
        text = done.stdout + done.stderr
        if done.returncode or (marker in text) != verbose or (out / "not-a-product.dll").exists():
            raise ValueError("CMake wrapper output probe failed")
        rows.append(dict(argv=command, verbose=verbose, exit=done.returncode, output=text))
    return rows


def prepare_relink(args):
    out = safe_output(args.output, fresh=True)
    base = Path(args.base).resolve(strict=True)
    old = Path(args.off_artifacts).resolve(strict=True)
    previous_plan = read(old / "plan.json")
    previous_result = read(old / "result.json")
    manual = read(old / "manual-identity-verification.json")
    verified = read(base.parent / "verification.json")
    if not verified["passed"] or not verified["crcEnabled"] or verified["refreshEnabled"]:
        raise ValueError("Original ON build is not verified")
    compile_result = [r for r in previous_result["commands"] if r["name"] == "compile"]
    if (len(compile_result) != 1 or compile_result[0]["exit"] or
            compile_result[0]["argv"] != previous_plan["compile"] or
            not manual["manualIdentityPassed"] or manual["changedInputs"]):
        raise ValueError("Historical OFF compile/input provenance is incomplete")
    normalized_off_compile = [a for a in previous_plan["compile"] if a != "/showIncludes"]
    def compile_signature(argv):
        return ["/Fo<OUTPUT>" if a.startswith("/Fo") else "/Fd<OUTPUT>" if a.startswith("/Fd")
                else "-D" + MACRO + "=<VALUE>" if a.startswith("-D" + MACRO + "=") else a for a in argv]
    if (compile_signature(previous_plan["originalCompile"]) != compile_signature(normalized_off_compile) or
            previous_plan["originalCompile"].count("-D" + MACRO + "=1") != 1 or
            normalized_off_compile.count("-D" + MACRO + "=0") != 1):
        raise ValueError("OFF object was compiled with unrelated option changes")
    expected = {key(r["path"]): r for r in verified["linkedObjects"]}
    on_object = expected[key(previous_plan["replacedObject"])]
    off_object = next(r for r in manual["products"] if Path(r["path"]).name == "model_crc.cpp.obj")
    historical = {key(r["path"]): r for r in previous_plan["inputs"]}
    original_link = previous_plan["originalLink"]
    environment = previous_plan["environment"]
    tools = [Path(original_link[0]), Path(previous_plan["originalCompile"][0]).parent / "lld-link.exe",
             Path(previous_plan["originalCompile"][0]).parent / "llvm-lib.exe",
             Path(previous_plan["originalCompile"][0]).parent / "llvm-pdbutil.exe"]
    tools += [Path(a.split("=", 1)[1]) for a in original_link if a.startswith(("--rc=", "--mt="))]
    prior_artifacts = [*verified["linkedObjects"], *verified["products"], *manual["products"],
                       previous_plan["staticLibrary"], environment, compile_result[0]["log"]]
    prior_artifacts += [historical[key(p)] for p in tools]
    # Validate binary provenance only. The current source layout has migrated;
    # it is deliberately not a relink input or revalidated as pre-migration code.
    verify(prior_artifacts)
    env = dict(os.environ)
    env.update(read(environment["path"]))
    libdirs = [base, *(Path(p) for p in env["LIB"].split(";") if p)]
    def library(name):
        for directory in libdirs:
            path = directory / name
            if path.is_file():
                return path.resolve()
        raise ValueError(f"Unresolved link library: {name}")
    # Include every prior observed PDB contributing library plus explicit link
    # libraries and /MT's defaults. Actual reads must remain within this set.
    names = {a for a in original_link if a.lower().endswith(".lib") and not a.startswith(("/", "-"))}
    names.update(("libcmt.lib", "libcpmt.lib", "libucrt.lib", "libvcruntime.lib", "oldnames.lib"))
    gap = read(old / "library-provenance-gap.json")
    names.update(Path(p).name for p in gap["pdbContributingLibraries"])
    libraries = {name.casefold(): identity(library(name)) for name in sorted(names)}
    explicit_manifests = []
    start = original_link.index("--manifests") + 1
    for value in original_link[start:original_link.index("--", start)]:
        path = (base / value).resolve(strict=True)
        explicit_manifests.append(historical[key(path)])
    verify(explicit_manifests)
    direct = [(base / a).resolve(strict=True) for a in original_link if a.lower().endswith(".obj")]
    if [key(p) for p in direct] != [key(p) for p in previous_plan["directObjects"]]:
        raise ValueError("Historical direct link order changed")
    llvm_lib = next(p for p in tools if p.name == "llvm-lib.exe")
    archive = previous_plan["staticLibrary"]
    members = subprocess.check_output([str(llvm_lib), "/list", archive["path"]], text=True).splitlines()
    if {key(base / p) for p in members if p.strip()} != {key(r["path"]) for r in previous_plan["archiveObjects"]}:
        raise ValueError("Static library member set does not match original objects")
    variants = []
    for name, crc in (("on", on_object), ("off", off_object)):
        destination = out / name
        command = []
        for arg in original_link:
            if arg.startswith("--intdir="):
                arg = "--intdir=" + str(destination / "manifest")
            elif arg.lower().startswith(("/out:", "/pdb:", "/implib:")):
                prefix, value = arg.split(":", 1)
                if Path(value).name != value:
                    raise ValueError("Unexpected historical output layout")
                arg = prefix + ":" + str(destination / value)
            elif arg.lower().endswith(".obj"):
                path = (base / arg).resolve(strict=True)
                arg = crc["path"] if key(path) == key(on_object["path"]) else str(path)
            elif arg.lower().endswith(".lib"):
                arg = libraries[arg.casefold()]["path"]
            elif arg in original_link[start:original_link.index("--", start)]:
                arg = str((base / arg).resolve(strict=True))
            command.append(arg)
        command.append("/verbose")
        variants.append(dict(name=name, output=str(destination), crcObject=crc, argv=command))
    plan = dict(mode="artifact-only-relink", output=str(out), base=str(base),
                historicalOffDirectory=str(old), originalObjects=verified["linkedObjects"],
                onObject=on_object, offObject=off_object, directObjects=[str(p) for p in direct],
                archiveObjects=previous_plan["archiveObjects"], staticLibrary=archive,
                environment=environment, environmentOverrides={"VERBOSE": "1", "VSLANG": "1033"},
                variants=variants, registeredLibraries=list(libraries.values()),
                explicitManifests=explicit_manifests, originalLink=original_link,
                historicalMacroConsumers=previous_plan["macroConsumers"],
                historicalMacroScanFiles=previous_plan["macroScanFiles"],
                currentProductSourcesRead=False, currentSourceBuild=False, sourceCopies=False,
                compilerWillRun=False, singleJob=True, preparedUnixNs=time.time_ns(),
                revision=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip())
    check_pair(plan)
    manifests = [old / p for p in ("plan.json", "result.json", "manual-identity-verification.json", "library-provenance-gap.json")]
    manifests += [base.parent / "verification.json", Path(__file__), ROOT / "tools/Inspect-MetaHookRuntime.py"]
    rows = [*prior_artifacts, *libraries.values(), *explicit_manifests, *(identity(p) for p in manifests)]
    plan["inputs"] = list({key(r["path"]): r for r in rows}.values())
    verify(plan["inputs"])
    out.mkdir(parents=True)
    plan["wrapperProbe"] = probe_wrapper(Path(original_link[0]), out, env)
    plan["wrapperEvidence"] = dict(
        url="https://github.com/Kitware/CMake/blob/v4.3.4/Source/cmcmd.cxx",
        sourceSha256="7e290325ad271c122aafff87e02736d7c2d0604d0b2d8bbdc7df8df89ba54416",
        implementation="VisualStudioLink reads VERBOSE; RunCommand prints successful output when verbose; --verbose is unsupported")
    save(out / "plan.json", plan)
    print(json.dumps(dict(prepared=True, mode=plan["mode"], inputs=len(plan["inputs"]),
                          libraries=len(libraries), originalObjects=len(plan["originalObjects"]),
                          variants=2, compiled=False, linked=False)))


def relink(args):
    out = safe_output(args.output)
    plan = read(out / "plan.json")
    if plan.get("mode") != "artifact-only-relink" or plan["output"] != str(out) or (out / "result.json").exists():
        raise ValueError("Wrong plan/output or previous result; prepare a fresh output")
    check_pair(plan)
    verify(plan["inputs"])
    # Refuse partial-output reuse: no existing product is ever overwritten.
    if any(Path(v["output"]).exists() for v in plan["variants"]):
        raise ValueError("Variant output already exists")
    env = dict(os.environ)
    env.update(read(plan["environment"]["path"]))
    env.update(plan["environmentOverrides"])
    registered = {key(r["path"]): r for r in plan["registeredLibraries"]}
    result = dict(passed=False, mode=plan["mode"], commands=[], variants=[],
                  compiled=False, currentSourceBuild=False, deployed=False, gameplay=False)
    spec = importlib.util.spec_from_file_location("runtime_pe", ROOT / "tools/Inspect-MetaHookRuntime.py")
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    tool = next(r["path"] for r in plan["inputs"] if Path(r["path"]).name == "llvm-pdbutil.exe")
    def run(name, argv, cwd, timeout=300):
        started = time.monotonic()
        done = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=timeout,
                              creationflags=subprocess.CREATE_NO_WINDOW)
        log = out / (name + ".log")
        log.write_text(done.stdout + done.stderr, encoding="utf-8")
        result["commands"].append(dict(name=name, argv=argv, exit=done.returncode,
                                        seconds=time.monotonic()-started, log=identity(log)))
        if done.returncode:
            raise RuntimeError(f"{name} failed: {log}")
        return done.stdout + done.stderr
    try:
        for variant in plan["variants"]:
            destination = Path(variant["output"])
            destination.mkdir()
            (destination / "manifest").mkdir()
            (destination / "temp").mkdir()
            env.update(TMP=str(destination / "temp"), TEMP=str(destination / "temp"))
            verify(plan["inputs"])
            trace = run("link-" + variant["name"], variant["argv"], destination)
            result.setdefault("linkedProducts", {})[variant["name"]] = record_link_products(
                destination, out / ("linked-products-" + variant["name"] + ".json"))
            reads = registered_library_reads(trace, registered)
            dll, pdb = destination / "CSOLSupport.dll", destination / "CSOLSupport.pdb"
            pe = audit.PE(dll)
            summary = run("pdb-" + variant["name"], [tool, "dump", "-summary", "-modules", str(pdb)], destination, 60)
            guid = re.search(r"GUID: \{([^}]+)\}", summary).group(1).lower()
            age = int(re.search(r"Age: (\d+)", summary).group(1))
            if not pe.codeview or (guid, age) != (pe.codeview["guid"], pe.codeview["age"]):
                raise ValueError("DLL/PDB identity mismatch")
            enabled = b"CSOL_CRC32_ProcessBuffer" in pe.data
            if enabled != (variant["name"] == "on") or b"GoldCraftCsolResource001" in pe.data:
                raise ValueError("CRC/refresh boundary does not match variant")
            contributors = sorted(set(re.findall(r"Obj: `([^`]+\.lib)`", summary, re.I)), key=key)
            if any(key(p) not in {key(r) for r in reads} for p in contributors):
                raise ValueError("PDB library contributor absent from actual read trace")
            verify(plan["inputs"])
            result["variants"].append(dict(name=variant["name"], x86=True, pdbGuid=guid, pdbAge=age,
                crcHookIdentifierPresent=enabled, actualLibraryReads=[identity(p) for p in reads],
                pdbContributingLibraries=contributors,
                products=[identity(p) for p in (dll, pdb, destination / "CSOLSupport.lib")]))
        result.update(passed=True, inputsUnchanged=True, originalObjectsUnchanged=len(plan["originalObjects"]),
                      onlyObjectSubstitution=plan["offObject"], equivalentLinkCommands=True)
    except Exception as error:
        result["failure"] = str(error)
        raise
    finally:
        save(out / "result.json", result)
    print(json.dumps(dict(passed=True, mode=plan["mode"], variants=result["variants"])))


def inspect_relinked_variant(variant, log, plan, destination):
    """Verify an existing product and actual trace without invoking any linker."""
    registered = {key(r["path"]): r for r in plan["registeredLibraries"]}
    reads = registered_library_reads(log.read_text(encoding="utf-8"), registered)
    spec = importlib.util.spec_from_file_location("runtime_pe", ROOT / "tools/Inspect-MetaHookRuntime.py")
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    product_dir = Path(variant["output"])
    dll, pdb = product_dir / "CSOLSupport.dll", product_dir / "CSOLSupport.pdb"
    pe = audit.PE(dll)
    tool = next(r["path"] for r in plan["inputs"] if Path(r["path"]).name == "llvm-pdbutil.exe")
    command = [tool, "dump", "-summary", "-modules", str(pdb)]
    done = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="strict",
                          timeout=60, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    pdb_log = destination / ("pdb-" + variant["name"] + ".log")
    with pdb_log.open("x", encoding="utf-8") as stream:
        stream.write(done.stdout)
    guid = re.search(r"GUID: \{([^}]+)\}", done.stdout).group(1).lower()
    age = int(re.search(r"Age: (\d+)", done.stdout).group(1))
    if not pe.codeview or (guid, age) != (pe.codeview["guid"], pe.codeview["age"]):
        raise ValueError("DLL/PDB identity mismatch")
    enabled = b"CSOL_CRC32_ProcessBuffer" in pe.data
    if enabled != (variant["name"] == "on") or b"GoldCraftCsolResource001" in pe.data:
        raise ValueError("CRC/refresh boundary does not match variant")
    contributors = sorted(set(re.findall(r"Obj: `([^`]+\.lib)`", done.stdout, re.I)), key=key)
    if any(key(p) not in {key(r) for r in reads} for p in contributors):
        raise ValueError("PDB contributor absent from actual library opens")
    return dict(name=variant["name"], passed=True, x86=True, pdbGuid=guid, pdbAge=age,
                actualLibraryReads=[identity(p) for p in reads], pdbContributingLibraries=contributors,
                products=[identity(p) for p in (dll, pdb, product_dir / "CSOLSupport.lib")],
                linkLog=identity(log), pdbCommand=command, pdbLog=identity(pdb_log),
                crcHookIdentifierPresent=enabled, refreshInterfaceAbsent=True)


def prepare_resume(args):
    out = safe_output(args.output, fresh=True)
    prior = safe_output(args.prior_relink)
    original = read(prior / "plan.json")
    failure = read(prior / "result.json")
    check_pair(original)
    if original["mode"] != "artifact-only-relink" or failure["passed"] or len(failure["commands"]) != 1:
        raise ValueError("Expected the preserved first ON post-link verification failure")
    completed = failure["commands"][0]
    if completed["name"] != "link-on" or completed["exit"] or completed["argv"] != original["variants"][0]["argv"]:
        raise ValueError("No exact successful ON link command")
    old_tool = next(r for r in original["inputs"] if key(r["path"]) == key(__file__))
    retained = [r for r in original["inputs"] if key(r["path"]) != key(__file__)]
    verify([*retained, completed["log"]])
    linked_products = failure.get("linkedProducts", {})
    if not isinstance(linked_products, dict):
        raise ValueError("Malformed linked product identities")
    anchored_products = linked_products.get("on")
    if "on" in linked_products and not isinstance(anchored_products, list):
        raise ValueError("Malformed ON product identities")
    anchor_file = prior / "linked-products-on.json"
    if anchor_file.exists():
        receipt_products = read(anchor_file)["products"]
        if not isinstance(receipt_products, list):
            raise ValueError("Malformed ON product receipt")
        if anchored_products is not None and anchored_products != receipt_products:
            raise ValueError("ON product receipts disagree")
        anchored_products = receipt_products
    if anchored_products is not None:
        expected = {key(Path(original["variants"][0]["output"]) / name) for name in
                    ("CSOLSupport.dll", "CSOLSupport.pdb", "CSOLSupport.lib")}
        if len(anchored_products) != 3 or {key(r["path"]) for r in anchored_products} != expected:
            raise ValueError("ON product receipt has unexpected paths")
        verify(anchored_products)
    if Path(original["variants"][1]["output"]).exists():
        raise ValueError("OFF already started; continuation must not overwrite it")
    current_tool = identity(__file__)
    out.mkdir(parents=True)
    on = inspect_relinked_variant(original["variants"][0], Path(completed["log"]["path"]), original, out)
    on_products = on["products"]
    if anchored_products is not None and on_products != anchored_products:
        raise ValueError("ON products changed after successful-link receipt")
    off = dict(original["variants"][1])
    off_output = out / "off"
    old_output = Path(off["output"])
    off["output"] = str(off_output)
    off["argv"] = ["--intdir=" + str(off_output / "manifest") if a.startswith("--intdir=")
                   else a.split(":", 1)[0] + ":" + str(off_output / Path(a.split(":", 1)[1]).name)
                   if a.lower().startswith(("/out:", "/pdb:", "/implib:")) else a for a in off["argv"]]
    if pair_link_signature(original["variants"][1]["argv"], old_output, Path(off["crcObject"]["path"])) != pair_link_signature(off["argv"], off_output, Path(off["crcObject"]["path"])):
        raise ValueError("Continuation changed OFF inputs or settings")
    references = [identity(prior / "plan.json"), identity(prior / "result.json"), completed["log"], identity(on["pdbLog"]["path"])]
    if anchor_file.exists():
        references.append(identity(anchor_file))
    inputs = [*retained, current_tool, *on_products, *references]
    verify(inputs)
    continuation = dict(mode="artifact-only-off-continuation", output=str(out), originalPlan=references[0],
        originalFailure=references[1], originalInputCount=len(original["inputs"]),
        retainedOriginalInputs=len(retained), originalTool=old_tool, currentTool=current_tool,
        originalToolChanged=old_tool != current_tool, onVerification=on, offVariant=off,
        onIdentityBoundary="successful-link-receipt" if anchored_products is not None else "first-captured-after-link-failure",
        inputs=list({key(r["path"]): r for r in inputs}.values()), compiled=False, linked=False,
        currentSourceBuild=False, onWillRelink=False, preparedUnixNs=time.time_ns())
    save(out / "plan.json", continuation)
    print(json.dumps(dict(prepared=True, onVerified=True, onLibraries=len(on["actualLibraryReads"]),
                          retainedOriginalInputs=len(retained), toolChanged=continuation["originalToolChanged"],
                          onWillRelink=False, offLinked=False)))


def resume_relink(args):
    out = safe_output(args.output)
    plan = read(out / "plan.json")
    if plan.get("mode") != "artifact-only-off-continuation" or plan["output"] != str(out) or (out / "result.json").exists():
        raise ValueError("Wrong continuation plan or existing result")
    verify(plan["inputs"])
    original = read(plan["originalPlan"]["path"])
    check_pair(original)
    off = plan["offVariant"]
    destination = Path(off["output"])
    if destination != out / "off" or destination.exists():
        raise ValueError("OFF output must be fresh and inside continuation directory")
    if pair_link_signature(off["argv"], destination, Path(off["crcObject"]["path"])) != pair_link_signature(original["variants"][1]["argv"], Path(original["variants"][1]["output"]), Path(original["offObject"]["path"])):
        raise ValueError("Changed continuation link command")
    env = dict(os.environ)
    env.update(read(original["environment"]["path"]))
    env.update(original["environmentOverrides"])
    destination.mkdir()
    (destination / "manifest").mkdir()
    (destination / "temp").mkdir()
    env.update(TMP=str(destination / "temp"), TEMP=str(destination / "temp"))
    result = dict(passed=False, mode=plan["mode"], on=plan["onVerification"], compiled=False,
                  onRelinked=False, currentSourceBuild=False, deployed=False, gameplay=False)
    try:
        start = time.monotonic()
        done = subprocess.run(off["argv"], cwd=destination, env=env, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=300,
                              creationflags=subprocess.CREATE_NO_WINDOW)
        log = out / "link-off.log"
        log.write_text(done.stdout + done.stderr, encoding="utf-8")
        result["command"] = dict(argv=off["argv"], exit=done.returncode, seconds=time.monotonic()-start, log=identity(log))
        if done.returncode:
            raise RuntimeError("OFF link failed")
        result["linkedProducts"] = record_link_products(destination, out / "linked-products-off.json")
        result["off"] = inspect_relinked_variant(off, log, original, out)
        verify(plan["inputs"])
        result.update(passed=True, continuationInputsUnchanged=True,
                      retainedOriginalInputsUnchanged=plan["retainedOriginalInputs"],
                      toolIdentityChange={"before": plan["originalTool"], "after": plan["currentTool"]},
                      originalObjectsUnchanged=213, onProductsUnchanged=True)
    except Exception as error:
        result["failure"] = str(error)
        raise
    finally:
        save(out / "result.json", result)
    print(json.dumps(dict(passed=True, onRelinked=False, off=result["off"]["products"])))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prepare", "build", "prepare-relink", "relink", "prepare-resume", "resume-relink"))
    parser.add_argument("--base", type=Path, default=ROOT / "build/csol-client-crc/client")
    parser.add_argument("--output", type=Path, default=ROOT / "build/csol-client-crc-baseline")
    parser.add_argument("--seed-plan", type=Path, default=ROOT / "build/engine-crc32/plan.json")
    parser.add_argument("--off-artifacts", type=Path, default=ROOT / "build/csol-client-crc-baseline")
    parser.add_argument("--prior-relink", type=Path, default=ROOT / "build/csol-client-crc-relink-20261107")
    arguments = parser.parse_args()
    {"prepare": prepare, "build": build, "prepare-relink": prepare_relink, "relink": relink,
     "prepare-resume": prepare_resume, "resume-relink": resume_relink}[arguments.mode](arguments)
