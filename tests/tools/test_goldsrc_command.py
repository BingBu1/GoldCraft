"""Transport-only regression checks; no server commands or credentials."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('goldsrc_command', Path(__file__).resolve().parents[2] / 'tools/GoldSrc-Command.py')
rcon = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rcon)


def fragment(sequence, index, count, payload):
    return b'\xfe\xff\xff\xff' + struct.pack('<IB', sequence, index << 4 | count) + payload


class ReplyTests(unittest.TestCase):
    def test_plain(self):
        reply = rcon.ReplyPackets()
        reply.feed(b'\xff\xff\xff\xfflFPS 100\n\0')
        reply.feed(b'\xff\xff\xff\xfflend\n\0')
        self.assertEqual(reply.finish(), 'FPS 100\nend\n')

    def test_reordered_duplicate_split_utf8(self):
        payload = b'\xff\xff\xff\xffl' + '怪物缓存统计\n'.encode() + b'\0'
        pieces = [payload[:7], payload[7:12], payload[12:]]
        reply = rcon.ReplyPackets()
        for index in (2, 0, 2, 1):
            reply.feed(fragment(42, index, 3, pieces[index]))
        self.assertEqual(reply.finish(), '怪物缓存统计\n')

    def test_interleaved_messages(self):
        reply = rcon.ReplyPackets()
        reply.feed(fragment(7, 0, 2, b'\xff\xff\xff\xfflfirst'))
        reply.feed(fragment(8, 1, 2, b'ond\0'))
        reply.feed(fragment(7, 1, 2, b'\0'))
        reply.feed(fragment(8, 0, 2, b'\xff\xff\xff\xfflsec'))
        self.assertEqual(reply.finish(), 'firstsecond')

    def test_missing_fragment(self):
        reply = rcon.ReplyPackets()
        reply.feed(fragment(5, 1, 2, b'end'))
        with self.assertRaises(TimeoutError): reply.finish()

    def test_invalid_packets(self):
        for packet in (b'\xfe\xff\xff\xff', fragment(1, 0, 0, b'x'), fragment(1, 2, 2, b'x')):
            with self.subTest(packet=packet), self.assertRaises(ValueError):
                rcon.ReplyPackets().feed(packet)

    def test_conflicting_fragment(self):
        for packet in (fragment(1, 0, 2, b'b'), fragment(1, 1, 3, b'b')):
            reply = rcon.ReplyPackets()
            reply.feed(fragment(1, 0, 2, b'a'))
            with self.assertRaises(ValueError): reply.feed(packet)

    def test_unexpected_type_and_budget(self):
        reply = rcon.ReplyPackets()
        reply.feed(b'not an RCON reply')
        with self.assertRaises(ValueError): reply.finish()
        with self.assertRaises(ValueError): rcon.ReplyPackets().feed(b'x' * (1024 * 1024 + 1))


if __name__ == '__main__':
    unittest.main()
