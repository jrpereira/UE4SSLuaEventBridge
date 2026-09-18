"""Exercise the test bundle allowlist with inert files; no real binaries packaged."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('test_bundle', Path(__file__).with_name('package-tests.py'))
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class BundleTests(unittest.TestCase):
    def test_allowlist_hashes_and_failure_paths(self):
        with tempfile.TemporaryDirectory(prefix='bridge-package-fixture-') as folder:
            root = Path(folder)
            expected = set(bundle.SOURCE_FILES) | set(bundle.BINARY_FILES)
            self.assertEqual(len(expected), len(bundle.SOURCE_FILES) + len(bundle.BINARY_FILES))
            private = ('benchmarks/runner/samples/session/result.json',
                       'benchmarks/runner/CONTINUATION.md', 'benchmarks/runner/CURRENT-MODEL.md',
                       'benchmarks/runner/SAMPLE_RUNS.md', 'benchmarks/runner/personal-config.json')
            for name in (*expected, *private):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('inert fixture: ' + name)
            output = root / 'out/bundle.zip'
            manifest = bundle.package(root, output)
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(set(archive.namelist()), expected | {'test-bundle-manifest.json'})
                self.assertEqual(json.loads(archive.read('test-bundle-manifest.json')), manifest)
                for name, digest in manifest['files'].items():
                    self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(), digest)
            self.assertEqual(output.with_suffix('.zip.sha256').read_text().strip(),
                             hashlib.sha256(output.read_bytes()).hexdigest())
            with self.assertRaises(FileExistsError):
                bundle.package(root, output)
            (root / bundle.BINARY_FILES[0]).unlink()
            missing_output = root / 'out/missing.zip'
            with self.assertRaises(ValueError):
                bundle.package(root, missing_output)
            self.assertFalse(missing_output.exists())


if __name__ == '__main__':
    unittest.main()
