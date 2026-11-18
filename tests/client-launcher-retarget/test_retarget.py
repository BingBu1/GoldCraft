"""Endpoint-only packaging must preserve the existing signed install identity."""
from pathlib import Path
import hashlib
import importlib.util
import json
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('retarget', ROOT / 'tools/client-launcher/retarget.py')
target = importlib.util.module_from_spec(spec)
spec.loader.exec_module(target)


class RetargetTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='retarget-test-')
        self.root = Path(self.temp.name)
        self.previous = self.root / 'previous'
        self.previous.mkdir()
        body = b'immutable embedded payload'
        self.config = dict(bundleId='stable', publicKeyPem='unchanged key', version='old',
                           updateBase='http://old.example/goldcraft/', corePaths=['MetaHook.dll'],
                           files=[dict(path='MetaHook.dll', bytes=len(body), sha256=hashlib.sha256(body).hexdigest())])
        (self.previous / 'bundle.json').write_text(json.dumps(self.config), 'utf-8')
        with zipfile.ZipFile(self.previous / 'payload.zip', 'w') as archive:
            archive.writestr('MetaHook.dll', body)

    def tearDown(self):
        self.temp.cleanup()

    def test_preserves_contract_and_immutable_payload(self):
        output = self.root / 'next'
        target.retarget(self.previous, output, 'http://new.example:28080/goldcraft/', 'new')
        result = json.loads((output / 'bundle.json').read_text('utf-8'))
        self.assertEqual({k for k in result if result[k] != self.config[k]}, {'version', 'updateBase'})
        self.assertEqual(json.loads((self.previous / 'bundle.json').read_text('utf-8')), self.config)
        self.assertTrue((output / 'payload.zip').samefile(self.previous / 'payload.zip'))

    def test_rejects_invalid_entries_before_output(self):
        for url in ['file:///goldcraft/', 'http://u:p@host/goldcraft/', 'http://host/other/',
                    'http://host/goldcraft/?x=1', 'http://host/goldcraft/#x', 'http://host:0/goldcraft/',
                    'http://host/goldcraft/?', 'http://host/goldcraft/\\x', 'http://host /goldcraft/']:
            with self.subTest(url=url), self.assertRaises(ValueError):
                target.retarget(self.previous, self.root / 'next', url, 'new')
        self.assertFalse((self.root / 'next').exists())

    def test_rejects_corrupt_payload_before_output(self):
        with zipfile.ZipFile(self.previous / 'payload.zip', 'w') as archive:
            archive.writestr('MetaHook.dll', b'corrupt')
        with self.assertRaises(ValueError):
            target.retarget(self.previous, self.root / 'next', 'http://host/goldcraft/', 'new')
        self.assertFalse((self.root / 'next').exists())

    def test_rejects_existing_output(self):
        with self.assertRaises(ValueError):
            target.retarget(self.previous, self.previous, 'http://host/goldcraft/', 'new')


if __name__ == '__main__':
    unittest.main()
