"""Exercise consistency rejection in the read-only native-cache observer."""
import importlib.util
from pathlib import Path
import struct
import unittest

SPEC = importlib.util.spec_from_file_location(
    "cache_observer", Path(__file__).resolve().parents[2] / "tools/Measure-ClientModelCache.py")
OBSERVER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(OBSERVER)


class Memory:
    base = 0x10000000
    hunk = 0x20000000
    owner = 0x30000000

    def __init__(self, empty=False):
        self.blocks = {}
        self.head = self.base + OBSERVER.HEAD
        self.node = self.hunk + 128
        header = bytearray(88)
        struct.pack_into("<4I", header, 72, *([self.head if empty else self.node] * 4))
        self.blocks[self.head] = header
        self.blocks[self.base + OBSERVER.HUNK_HIGH] = bytearray(struct.pack("<4I", 64, 128, 4096, self.hunk))
        if not empty:
            node = bytearray(88)
            struct.pack_into("<2I", node, 0, 160, self.owner)
            node[8:13] = b"model"
            struct.pack_into("<4I", node, 72, *([self.head] * 4))
            self.blocks[self.node] = node
            self.blocks[self.owner] = bytearray(struct.pack("<I", self.node + 88))

    def read(self, address, size):
        result = bytes(self.blocks[address])
        if len(result) != size:
            raise OSError("unexpected read")
        return result

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]


class SnapshotTests(unittest.TestCase):
    def test_allocation_includes_header_and_capacity_excludes_hunks(self):
        result = OBSERVER.snapshot(Memory(), Memory.base)
        self.assertEqual(result["allocatedBytes"], 160)
        self.assertEqual(result["cacheCapacityBytes"], 3904)
        self.assertEqual(result["unallocatedBytes"], 3744)
        self.assertEqual(result["nodes"][0]["name"], "model")

    def test_empty_chain(self):
        self.assertEqual(OBSERVER.snapshot(Memory(empty=True), Memory.base)["nodes"], [])

    def test_reject_owner_mismatch(self):
        memory = Memory()
        struct.pack_into("<I", memory.blocks[memory.owner], 0, memory.node)
        with self.assertRaisesRegex(ValueError, "owner"):
            OBSERVER.snapshot(memory, memory.base)

    def test_reject_cycle(self):
        memory = Memory()
        struct.pack_into("<I", memory.blocks[memory.node], 0x4C, memory.node)
        with self.assertRaisesRegex(ValueError, "cyclic"):
            OBSERVER.snapshot(memory, memory.base)

    def test_reject_lru_mismatch(self):
        memory = Memory()
        struct.pack_into("<I", memory.blocks[memory.node], 0x50, memory.node)
        with self.assertRaisesRegex(ValueError, "LRU"):
            OBSERVER.snapshot(memory, memory.base)

    def test_reject_out_of_bounds_block(self):
        memory = Memory()
        struct.pack_into("<I", memory.blocks[memory.node], 0, 8192)
        with self.assertRaisesRegex(ValueError, "size"):
            OBSERVER.snapshot(memory, memory.base)

    def test_reject_second_read_change(self):
        memory = Memory()
        original_read = memory.read
        reads = 0

        def changing_read(address, size):
            nonlocal reads
            if address == memory.node:
                reads += 1
                if reads == 2:
                    memory.blocks[address][8] = ord("X")
            return original_read(address, size)

        memory.read = changing_read
        with self.assertRaisesRegex(ValueError, "changed during"):
            OBSERVER.snapshot(memory, memory.base)


if __name__ == "__main__":
    unittest.main()
