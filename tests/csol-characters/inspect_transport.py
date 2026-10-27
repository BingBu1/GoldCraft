"""Read received entity states using the deployed CSOL DLL's matching PDB.

Read-only process access: no remote calls, code injection or memory writes.
The engine GetEntityByIndex implementation supplies the verified entity array.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('audit', ROOT / 'tools/Inspect-MetaHookRuntime.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class Inspector:
    def __init__(self, pid, pdb):
        self.process = audit.Process(pid, 'cs-client-b')
        self.pe = audit.PE(ROOT / 'sandbox/cs-client-b/Half-Life/cstrike/metahook/plugins/CSOLSupport.dll')
        symbols = audit.Symbols(pdb, self.pe)
        module = next(m for m in self.process.modules if m['path'] == self.pe.path)
        assert self.process.read(module['base'] + self.pe.codeview['rva'], 24).hex() == self.pe.codeview['identity']
        self.entity = symbols.layout('cl_entity_s')
        self.state = symbols.layout('entity_state_s')
        api = module['base'] + symbols.global_rva('csol::client::engine')
        function = self.process.u32(api + symbols.layout('cl_enginefuncs_s')['members']['GetEntityByIndex'])
        code = self.process.read(function, 49)
        # Exact instruction shape: bounds check; imul by sizeof(cl_entity_t);
        # add [entity array]. Resolve both globals from current process code.
        assert code[:6] == bytes.fromhex('558bec8d4508')
        assert code[23:25] == bytes.fromhex('3b05')
        assert code[31:33] == bytes.fromhex('69c0')
        assert code[37:39] == bytes.fromhex('0305')
        assert struct.unpack_from('<I', code, 33)[0] == self.entity['size']
        self.limit_address = struct.unpack_from('<I', code, 25)[0]
        self.array_address = struct.unpack_from('<I', code, 39)[0]
        hw = next(m for m in self.process.modules if m['path'].name.lower() == 'hw.dll')
        assert hashlib.sha256(hw['path'].read_bytes()).hexdigest() == '9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295'
        assert hw['base'] <= function < hw['base'] + hw['size']
        self.identity = dict(dllSHA256=self.pe.sha256, pdb=symbols.identity,
                             function=self.process.location(function), instructions=code.hex(),
                             entityLayout=self.entity, stateLayout=self.state)

    def read(self):
        r = self.process
        base = r.u32(self.array_address)
        limit = r.u32(self.limit_address)
        assert 33 <= limit <= 65536 and base
        result = []
        for i in range(1, 33):
            data = r.read(base + i * self.entity['size'], self.entity['size'])
            assert struct.unpack_from('<I', data)[0] in (0, i)
            start = self.entity['members']['curstate']
            values = {name: struct.unpack_from('<i', data, start + self.state['members'][name])[0]
                      for name in ('number', 'messagenum', 'modelindex', 'sequence', 'gaitsequence', 'effects')}
            result.append(dict(index=i, **values))
        return result

    def close(self):
        self.process.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--pdb', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    inspector = Inspector(args.pid, args.pdb)
    try:
        result = dict(identity=inspector.identity, entities=inspector.read())
        args.out.write_text(json.dumps(result, indent=2), encoding='utf8')
        print([(r['index'], r['sequence'], r['gaitsequence']) for r in result['entities'] if r['modelindex']])
    finally:
        inspector.close()
