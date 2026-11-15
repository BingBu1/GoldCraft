#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read an identity-matched client's admission state without issuing game input.

The process handle permits queries and memory reads only. The installation is
never written; all output must be a fresh file in the workspace build directory.
Private symbol offsets come from the supplied matching PDB, not saved addresses.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import importlib.util
import json
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parent.parent


class ReadOnlyClient:
    def __init__(self, pid, installation):
        self.kernel = C.WinDLL('kernel32', use_last_error=True)
        k = self.kernel
        k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
        k.OpenProcess.restype = W.HANDLE
        k.CloseHandle.argtypes = [W.HANDLE]
        k.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]
        k.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
        self.handle = k.OpenProcess(0x410, False, pid)
        if not self.handle:
            raise C.WinError(C.get_last_error())
        try:
            name = C.create_unicode_buffer(32768)
            count = W.DWORD(len(name))
            if not k.QueryFullProcessImageNameW(self.handle, 0, name, C.byref(count)):
                raise C.WinError(C.get_last_error())
            self.executable = Path(name.value).resolve(strict=True)
            if self.executable.parent != installation or self.executable.name.lower() != 'metahook.exe':
                raise ValueError('PID is not the specified MetaHook client')
            ps = C.WinDLL('psapi', use_last_error=True)
            ps.EnumProcessModulesEx.argtypes = [W.HANDLE, C.POINTER(W.HMODULE), W.DWORD, C.POINTER(W.DWORD), W.DWORD]
            ps.GetModuleFileNameExW.argtypes = [W.HANDLE, W.HMODULE, W.LPWSTR, W.DWORD]
            modules, size = (W.HMODULE * 1024)(), W.DWORD()
            if not ps.EnumProcessModulesEx(self.handle, modules, C.sizeof(modules), C.byref(size), 3):
                raise C.WinError(C.get_last_error())
            if size.value > C.sizeof(modules):
                raise ValueError('Module list exceeds observation bound')
            self.modules = []
            for module in modules[:size.value // C.sizeof(W.HMODULE)]:
                name = C.create_unicode_buffer(32768)
                if not ps.GetModuleFileNameExW(self.handle, module, name, len(name)):
                    raise C.WinError(C.get_last_error())
                # A loaded driver/overlay image may already have been removed
                # from disk. Only the selected CSOL image must exist below.
                self.modules.append((Path(name.value).resolve(), module))
        except BaseException:
            self.close()
            raise

    def close(self):
        self.kernel.CloseHandle(self.handle)

    def read(self, address, count):
        if not 0 < count <= 1024 * 1024 or not 0 < address < 2**32:
            raise ValueError('Read outside bounded x86 observation')
        result, received = C.create_string_buffer(count), C.c_size_t()
        if not self.kernel.ReadProcessMemory(self.handle, address, result, count, C.byref(received)) or received.value != count:
            raise C.WinError(C.get_last_error())
        return result.raw

    def u32(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--installation', type=Path, required=True)
    parser.add_argument('--pdb', type=Path, required=True)
    parser.add_argument('--goldcraft-pdb', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    installation, pdb = args.installation.resolve(strict=True), args.pdb.resolve(strict=True)
    output = args.output.absolute()
    if (output.exists() or not output.resolve().is_relative_to(ROOT / 'build') or
            not pdb.is_relative_to(ROOT) or any(p.is_symlink() or p.is_junction() for p in (output, *output.parents))):
        raise ValueError('Use workspace symbols and a fresh, non-junction build output')
    spec = importlib.util.spec_from_file_location('audit', ROOT / 'tools/Inspect-MetaHookRuntime.py')
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    process = ReadOnlyClient(args.pid, installation)
    try:
        image = installation / 'cstrike/metahook/plugins/CSOLSupport.dll'
        matches = [base for path, base in process.modules if path == image.resolve(strict=True)]
        if len(matches) != 1:
            raise ValueError('Expected one installed CSOLSupport module')
        base, pe = matches[0], audit.PE(image)
        symbols = audit.Symbols(pdb, pe)
        def identity():
            return process.read(base + pe.codeview['rva'], 24).hex() == pe.codeview['identity']
        if not identity():
            raise ValueError('Loaded client does not match the supplied symbols')
        units = re.findall(r'Mod (\d+) \| `([^`]+)`', symbols.dump('-modules'))
        fields = {
            'guest_cof_admission': {'admittedWeapons': 4, 'generation': 8, 'resources': 8,
                                    'protocol': 4, 'weaponMask': 4, 'connection': 4},
            'guest_cof_resources': {'state': 1, 'subscription': 8},
            'guest_cof_scene': {'pointPrepared': 1, 'pointSceneReady': 1, 'pointSceneValidated': 1,
                                'pointContext': 4},
            'guest_cof_muzzle': {'prepared': 1, 'eventInstalled': 1},
            'guest_cof_effects': {'prepared': 1},
            'guest_cof_particles': {'ready': 1},
        }
        observed, unavailable = {}, []
        for unit, requested in fields.items():
            matched = [int(i) for i, name in units if name.lower().endswith((unit + '.obj', unit + '.cpp.obj'))]
            if len(matched) != 1:
                raise ValueError('Missing or ambiguous compilation unit: ' + unit)
            text = symbols.dump('-symbols', '-modi=' + str(matched[0]))
            for name, size in requested.items():
                match = re.search(r'S_LDATA32[^\n]*::' + re.escape(name) + r'`\s*\n[^\n]*addr = (\d+):(\d+)', text)
                key = unit + '::' + name
                if not match:
                    unavailable.append(key)
                    continue
                section = int(match[1])
                if not 0 < section <= len(pe.sections):
                    raise ValueError('Invalid symbol section')
                address = base + pe.sections[section - 1]['rva'] + int(match[2])
                observed[key] = {'address': address, 'value': int.from_bytes(process.read(address, size), 'little')}
        cvars = {}
        for key in ('protocol', 'weaponMask'):
            entry = observed.get('guest_cof_admission::' + key)
            if not entry or not entry['value']:
                continue
            value = process.read(entry['value'], 20)
            cvars[key] = {'flags': struct.unpack_from('<i', value, 8)[0],
                          'value': struct.unpack_from('<f', value, 12)[0]}
        resource_state = None
        state_entry = observed.get('guest_cof_resources::state')
        if state_entry:
            layout = symbols.layout('csol::client::GuestCofResourceState')
            data = process.read(state_entry['address'], layout['size'])
            member = layout['members']
            resource_state = {'status': data[member['status']],
                              'generation': struct.unpack_from('<Q', data, member['generation'])[0]}
            for field in ('attempts', 'samples', 'weapons'):
                resource_state[field] = struct.unpack_from('<I', data, member[field])[0]
            for field in ('studioAndRecoilPrepared', 'decalsPrepared'):
                resource_state[field] = bool(data[member[field]])
            resource = struct.unpack_from('<I', data, member['resource'])[0]
            if resource:
                resource_state['resource'] = process.read(resource, 256).split(b'\0', 1)[0].decode('utf-8', 'strict')
        sound_table = None
        if args.goldcraft_pdb:
            gold_pdb = args.goldcraft_pdb.resolve(strict=True)
            if not gold_pdb.is_relative_to(ROOT):
                raise ValueError('Use workspace GoldCraft symbols')
            gold_image = installation / 'cstrike/metahook/plugins/GoldCraft.dll'
            gold_matches = [b for path, b in process.modules if path == gold_image.resolve(strict=True)]
            if len(gold_matches) != 1:
                raise ValueError('Expected one GoldCraft module')
            gold_base, gold_pe = gold_matches[0], audit.PE(gold_image)
            gold_symbols = audit.Symbols(gold_pdb, gold_pe)
            if process.read(gold_base + gold_pe.codeview['rva'], 24).hex() != gold_pe.codeview['identity']:
                raise ValueError('Loaded GoldCraft differs from its symbols')
            gold_units = re.findall(r'Mod (\d+) \| `([^`]+)`', gold_symbols.dump('-modules'))
            matched = [int(i) for i, name in gold_units if name.lower().endswith(('precache_client.obj', 'precache_client.cpp.obj'))]
            if len(matched) != 1:
                raise ValueError('Missing GoldCraft precache unit')
            text = gold_symbols.dump('-symbols', '-modi=' + str(matched[0])) + '\n' + gold_symbols.globals
            def read_symbol(name, size):
                match = re.search(r'S_[LG]DATA32[^\n]*::' + re.escape(name) + r'`\s*\n[^\n]*addr = (\d+):(\d+)', text)
                if not match:
                    raise ValueError('Missing GoldCraft symbol: ' + name)
                address = gold_base + gold_pe.sections[int(match[1]) - 1]['rva'] + int(match[2])
                return process.read(address, size)
            sound_table = {'imageSha256': gold_pe.sha256}
            for name in ('sound_connection_state', 'sound_signon', 'server_count'):
                address = int.from_bytes(read_symbol(name, 4), 'little')
                sound_table[name] = process.u32(address) if address else None
            for name in ('enabled', 'extended', 'receiving', 'sound_query_retiring'):
                try:
                    sound_table[name] = bool(read_symbol(name, 1)[0])
                except ValueError:
                    sound_table[name] = None
            sound_table['connectionGeneration'] = int.from_bytes(read_symbol('connection_generation_value', 8), 'little')
            try:
                vector_data = read_symbol('sounds', 12) if sound_table['extended'] else None
            except ValueError:
                vector_data = None
            if sound_table['extended'] and not vector_data:
                # ThinLTO can split the vector's begin/end globals and omit
                # their debug names. Resolve only the paired loads inside the
                # exact PDB-identified query; reject any different code shape.
                function = re.search(r'S_LPROC32[^\n]*::inspect_sound_precache`\s*\n[^\n]*addr = (\d+):(\d+), code size = (\d+)', text)
                if function:
                    rva = gold_pe.sections[int(function[1]) - 1]['rva'] + int(function[2])
                    start = gold_pe.offset(rva)
                    code = gold_pe.data[start:start + int(function[3])]
                    loads = list(re.finditer(rb'\x8b\x0d(.{4})\x8b\x35(.{4})\x29\xce\xc1\xfe\x02', code, re.DOTALL))
                    if len(loads) != 1:
                        raise ValueError('Unknown optimized sound vector query shape')
                    preferred = gold_pe.u32(gold_pe.u32(0x3c) + 24 + 28)
                    locations = [int.from_bytes(loads[0][i], 'little') - preferred for i in (1, 2)]
                    if any(not 0 <= x <= gold_pe.image_size - 4 for x in locations):
                        raise ValueError('Sound vector address outside matched image')
                    vector_data = b''.join(process.read(gold_base + x, 4) for x in locations)
                    sound_table['vectorResolution'] = {'queryRva': rva, 'queryBytes': code.hex(),
                                                       'pointerRvas': locations}
            if vector_data:
                first, last = struct.unpack_from('<II', vector_data)
                if last < first or (last - first) % 4 or (last - first) // 4 > 65536:
                    raise ValueError('Invalid bounded extended sound table')
                pointers = process.read(first, last - first)
            else:
                address = int.from_bytes(read_symbol('legacy_sound_precache', 4), 'little')
                pointers = process.read(address, 512 * 4)
                sound_table['tableScope'] = 'legacy first 512 slots; extended vector optimized out of PDB'
            guest_sounds = []
            registered = 0
            for index in range(len(pointers) // 4):
                address = struct.unpack_from('<I', pointers, index * 4)[0]
                if not address:
                    continue
                data = process.read(address, 72)
                registered += 1
                name = data[:64].split(b'\0', 1)[0].decode('utf-8', 'strict')
                if name.startswith('guest/cof/'):
                    guest_sounds.append({'index': index, 'name': name,
                                         'servercount': struct.unpack_from('<i', data, 68)[0]})
            sound_table.update(slots=len(pointers) // 4, nonempty=registered, guestSounds=guest_sounds)
            sound_table['identityStable'] = process.read(gold_base + gold_pe.codeview['rva'], 24).hex() == gold_pe.codeview['identity']
        result = dict(pid=args.pid, imageSha256=pe.sha256, pdb=symbols.identity, moduleBase=base,
                      observed=observed, unavailable=unavailable, cvars=cvars, resourceState=resource_state,
                      soundTable=sound_table,
                      identityStable=identity(), readOnly=True, noInput=True, noThreadSuspension=True,
                      boundary='External reads are not an atomic frame or a gameplay acceptance result')
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(result, indent=2))
    finally:
        process.close()


if __name__ == '__main__':
    main()
