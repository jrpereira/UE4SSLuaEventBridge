import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('preflight', Path(__file__).resolve().parents[1] / 'tools/deployment_preflight.py')
preflight = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preflight)

class PreflightTests(unittest.TestCase):
    def test_same_version_different_bytes_and_running_game(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            staged, deployed = root / 'staged', root / 'deployed'
            staged.mkdir(); deployed.mkdir()
            (staged / 'main.lua').write_text('new')
            (deployed / 'main.lua').write_text('old')
            manifest = {'module': 'Example', 'version': '1.2.3', 'files': {'main.lua': preflight.digest(staged / 'main.lua')}}
            session = {'pid': 7, 'started_at': '2026-09-18T10:00:00Z'}
            result = preflight.compare(manifest, staged, deployed, session)
            self.assertEqual(result['deployed_differences'], ['main.lua'])
            self.assertTrue(result['game_running'])
            self.assertTrue(result['needs_attention'])
            result = preflight.compare(manifest, staged, staged, session)
            self.assertTrue(result['needs_attention'])
            result = preflight.compare(manifest, staged, staged)
            self.assertFalse(result['game_running'])
            self.assertFalse(result['needs_attention'])
            result = preflight.compare(manifest, staged, staged, working=deployed,
                                       working_version='1.2.4')
            self.assertEqual(result['working_differences'], ['main.lua'])
            self.assertTrue(result['staged_version_differs_from_working'])
            self.assertTrue(result['needs_attention'])

    def test_stage_tamper_and_path_escape(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest = {'module': 'Example', 'version': '1.2.3', 'files': {'main.lua': 'wrong'}}
            result = preflight.compare(manifest, root, root)
            self.assertEqual(result['stage_integrity_errors'], ['main.lua'])
            for path in ['../config.ini', '/absolute', 'C:/other', 'dir\\file']:
                with self.assertRaises(ValueError):
                    preflight.safe_file(root, path)

if __name__ == '__main__':
    unittest.main()
