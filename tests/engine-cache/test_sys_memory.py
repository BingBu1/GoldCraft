#!/usr/bin/env python3
"""Run actual Sys_InitMemory/Sys_ShutdownMemory bodies against OS allocation stubs."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "external/ReHLDS/rehlds/engine/sys_dll2.cpp"
HEADER = ROOT / "external/ReHLDS/rehlds/engine/sys_dll2.h"

STUBS = r'''
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
using ULONGLONG=unsigned long long;
using HGLOBAL=void*;
struct MEMORYSTATUSEX { unsigned dwLength{}; ULONGLONG ullTotalPhys{}; };
struct HostParms { void* membase{}; int memsize{}; } host_parms;
bool g_bIsDedicatedServer=true, memoryStatusOK=true;
ULONGLONG physicalMemory=8ull*1024*1024*1024;
std::vector<std::string> arguments;
std::vector<char*> pointers;
char** com_argv{};
int com_argc{}, checks{}, failAllocation{}, allocCalls{};
std::vector<int> requestedBytes;
std::vector<std::string> events;
std::vector<void*> liveAllocations;
constexpr int GMEM_FIXED=0;
#define Q_min std::min
#define Q_max std::max
int COM_CheckParm(const char* option) { for(int i=1;i<com_argc;++i) if(arguments[i]==option) return i; return 0; }
[[noreturn]] void Sys_Error(const char* format, ...) {
    char text[1024];va_list ap;va_start(ap,format);std::vsnprintf(text,sizeof(text),format,ap);va_end(ap);
    throw std::runtime_error(text);
}
bool GlobalMemoryStatusEx(MEMORYSTATUSEX* status) { status->ullTotalPhys=physicalMemory; return memoryStatusOK; }
void* GlobalAlloc(unsigned, int bytes) {
    requestedBytes.push_back(bytes);++allocCalls;events.push_back("alloc");
    if(allocCalls==failAllocation) return nullptr;
    auto* result=reinterpret_cast<void*>(uintptr_t(allocCalls*4096));liveAllocations.push_back(result);return result;
}
void* GlobalFree(HGLOBAL pointer) {
    events.push_back(pointer ? "free" : "free-null");
    if(pointer) {
        auto found=std::find(liveAllocations.begin(),liveAllocations.end(),pointer);
        if(found==liveAllocations.end()) throw std::runtime_error("free of unknown allocation");
        liveAllocations.erase(found);
    }
    return nullptr;
}
void Memory_Shutdown() { events.push_back("memory-shutdown"); }
void Sys_ShutdownMemory();
'''

TESTS = r'''
void Check(bool value,const char* label) { ++checks;if(!value)throw std::runtime_error(label); }
void Args(std::initializer_list<const char*> list) {
    arguments.clear();pointers.clear();arguments.emplace_back("fixture");
    for(auto* word:list) arguments.emplace_back(word);
    for(auto& word:arguments)pointers.push_back(word.data());
    com_argc=int(pointers.size());com_argv=pointers.data();
    requestedBytes.clear();events.clear();allocCalls=failAllocation=0;memoryStatusOK=true;physicalMemory=8ull*1024*1024*1024;
}
void Verify(int expected) {
    Sys_InitMemory();
    Check(host_parms.memsize==64*1024*1024,"fixed 64MiB hunk");
    Check(Sys_CacheMemorySize()==expected && Sys_CacheMemory()!=nullptr,"selected independent cache");
    Check(host_parms.membase!=Sys_CacheMemory(),"distinct allocations");
    Check(requestedBytes.size()==2 && requestedBytes[0]==64*1024*1024 && requestedBytes[1]==expected,"OS requests");
    events.clear();Sys_ShutdownMemory();
    Check(events.size()==3 && events[0]=="memory-shutdown" && events[1]=="free" && events[2]=="free","owner reset precedes both frees");
    Check(!host_parms.membase && !host_parms.memsize && !Sys_CacheMemory() && !Sys_CacheMemorySize() && liveAllocations.empty(),"shutdown reset");
}
void Fatal() {
    bool caught=false;try {Sys_InitMemory();}catch(const std::runtime_error&){caught=true;}
    Check(caught,"expected input/allocation error");
    Check(!host_parms.membase && !Sys_CacheMemory() && liveAllocations.empty(),"error leaves no allocated arenas");
}
int main() {
    try {
        Args({});Verify(80*1024*1024);
        g_bIsDedicatedServer=false;Args({});Verify(256*1024*1024);
        g_bIsDedicatedServer=true;Args({"-heapsize","65536"});Verify(64*1024*1024);
        Args({"-heapsize","262144"});Verify(80*1024*1024);
        Args({"-heapsize","14336"});Verify(14*1024*1024);
        Args({"-heapsize","14335"});Verify(80*1024*1024);
        Args({"-heapsize","65536","-minmemory"});Verify(14*1024*1024);
        Args({"-heapsize"});Verify(80*1024*1024);
        Args({"-minmemory"});Verify(14*1024*1024);
        for(auto* value:{"0","-1","junk","65536junk","2147483647","99999999999999999999999999",""}) {Args({"-heapsize",value});Fatal();Check(allocCalls==0,"invalid input precedes allocation");}
        Args({});physicalMemory=255ull*1024*1024;Fatal();
        Args({});memoryStatusOK=false;Fatal();
        Args({});failAllocation=1;Fatal();Check(events[2]=="memory-shutdown","first allocation failure cleans up before free");
        Args({});failAllocation=2;Fatal();Check(events[2]=="memory-shutdown","second allocation failure cleans up before free");
        Args({});Verify(80*1024*1024);
        events.clear();Sys_ShutdownMemory();Check(liveAllocations.empty(),"repeat shutdown harmless");
        std::printf("sys-memory checks=%d passed; actual extracted bodies; OS stubs; no live FPS evidence\n",checks);return 0;
    } catch(const std::exception& error) {std::printf("failure checks=%d %s\n",checks,error.what());return 1;}
}
'''

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--out", type=Path, default=ROOT / "build/engine-cache-20261029")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    data = SOURCE.read_text(encoding="utf-8")
    start = data.index("namespace\n{\nvoid *modelCacheMemory;")
    end = data.index("void Sys_InitLauncherInterface", start)
    body = data[start:end]
    constants = "\n".join(re.findall(r"^#define\s+(?:MINIMUM_WIN_MEMORY|DEFAULT_MEMORY|DEFAULT_CACHE_MEMORY|MAXIMUM_CACHE_MEMORY)\s+.*$", HEADER.read_text(encoding="utf-8"), re.M))
    source = args.out / "sys-memory-fixture.cpp"
    source.write_text(STUBS + "\n" + constants + "\n" + body + TESTS, encoding="utf-8")
    exe = args.out / "sys-memory-fixture.exe"
    command = [args.compiler, "--target=i686-pc-windows-msvc", "-fuse-ld=lld", "/nologo", "/std:c++20", "/EHsc", "/MT", "/O2", "/clang:-O3", "/arch:AVX2", "/fp:precise", str(source), "/Fo" + str(args.out / "sys-memory-fixture.obj"), "/Fe" + str(exe), "/link", "/threads:1"]
    built = subprocess.run(command, capture_output=True, text=True)
    (args.out / "sys-memory-build.log").write_text(built.stdout + built.stderr, encoding="utf-8")
    if built.returncode:
        print(built.stdout + built.stderr)
        raise SystemExit(built.returncode)
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    (args.out / "sys-memory-result.log").write_text(run.stdout + run.stderr, encoding="utf-8")
    print(run.stdout + run.stderr)
    result = {"productionSourceSHA256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(), "headerSHA256": hashlib.sha256(HEADER.read_bytes()).hexdigest(), "compileCommand": command, "runExitCode": run.returncode, "bodyUnchanged": True, "osAllocationStubs": True, "liveTest": False}
    (args.out / "sys-memory-results.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    raise SystemExit(run.returncode)

if __name__ == "__main__":
    main()
