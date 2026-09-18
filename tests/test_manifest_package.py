import importlib.util
from pathlib import Path
import json
import sys
import tempfile
import unittest
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import package_manifest

class ManifestPackageTests(unittest.TestCase):
    def test_versions(self):
        for value in ['0.3.53', '0.3.53-native.1', '1.2.3-rc.2', '1.2.3+build.7']:
            self.assertTrue(package_manifest.valid_version(value), value)
        for value in ['01.2.3', '1.2.3-rc.01', '1.2', 'v1.2.3', '1.2.3/other']:
            self.assertFalse(package_manifest.valid_version(value), value)

    def test_manifest_excludes_extra_files_and_rejects_missing_required_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'Scripts').mkdir()
            (root / 'Scripts/main.lua').write_text('local VERSION="1.2.3-rc.1"')
            spec = {'module': 'Example', 'version_file': 'Scripts/main.lua', 'version_pattern': 'VERSION="([^"]+)"', 'files': {'Scripts/main.lua': 'Scripts/main.lua'}}
            (root / 'release-manifest.json').write_text(json.dumps(spec))
            (root / 'Scripts/probe.lua').write_text('temporary')
            (root / 'config.ini').write_text('personal')
            archive = package_manifest.build(root)
            first = archive.read_bytes()
            self.assertEqual(first, package_manifest.build(root).read_bytes())
            with ZipFile(archive) as bundle:
                self.assertEqual(set(bundle.namelist()), {'Example/Scripts/main.lua', 'Example/manifest.json'})
            (root / 'Scripts/main.lua').unlink()
            with self.assertRaises(FileNotFoundError):
                package_manifest.build(root)

if __name__ == '__main__':
    unittest.main()
