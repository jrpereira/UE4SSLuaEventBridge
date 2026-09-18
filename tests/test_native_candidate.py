import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import native_candidate as native
from release_session import inspect_candidate


class NativeCandidateTests(unittest.TestCase):
    def fixture(self, root):
        (root/'mod/include/UE4SSLuaEventBridge').mkdir(parents=True)
        (root/'mod/include/UE4SSLuaEventBridge/Version.hpp').write_text('#define UE4SSLEB_VERSION "1.2.3"')
        (root/'CMakeLists.txt').write_text('fixture')
        return root/'build/candidate'

    def runner(self, output):
        def run(command, **kwargs):
            if command[:2] == ['cmake','--build']:
                staged = output/'dist/UE4SSLuaEventBridge'
                (staged/'dlls').mkdir(parents=True)
                (staged/'dlls/main.dll').write_bytes(b'fixture dll')
                (staged/'enabled.txt').write_bytes(b'')
        return run

    def test_dirty_same_version_source_and_new_headers_are_detected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = self.fixture(root)
            path = native.build(root, output, self.runner(output))
            manifest = json.loads(path.read_text())
            self.assertEqual(native.validate(manifest,root), [])
            (root/'tests/__pycache__').mkdir(parents=True)
            (root/'tests/__pycache__/cached.pyc').write_bytes(b'generated cache')
            self.assertEqual(native.validate(manifest,root), [])
            staged = output/'dist/UE4SSLuaEventBridge'
            result = inspect_candidate(manifest,staged,staged,root)
            self.assertFalse(result['needs_attention'])
            (root/'mod/new.hpp').write_text('new header')
            result = inspect_candidate(manifest,staged,staged,root)
            self.assertTrue(result['needs_attention'])
            self.assertEqual(result['working_differences'], ['mod/new.hpp'])
            self.assertFalse(result['staged_version_differs_from_working'])
            with self.assertRaises(ValueError):
                native.build(root, output, self.runner(output))

    def test_failed_tests_never_certify_candidate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = self.fixture(root)
            def run(command, **kwargs):
                self.runner(output)(command, **kwargs)
                if command[0] == 'ctest':
                    raise subprocess.CalledProcessError(1, command)
            with self.assertRaises(subprocess.CalledProcessError):
                native.build(root,output,run)
            self.assertFalse((output/'candidate-manifest.json').exists())

    def test_source_edit_during_build_never_certifies_candidate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            output = self.fixture(root)
            def run(command, **kwargs):
                self.runner(output)(command, **kwargs)
                if command[0] == 'ctest':
                    (root/'mod/edited.cpp').write_text('changed during build')
            with self.assertRaises(RuntimeError):
                native.build(root,output,run)
            self.assertFalse((output/'candidate-manifest.json').exists())


if __name__ == '__main__':
    unittest.main()
