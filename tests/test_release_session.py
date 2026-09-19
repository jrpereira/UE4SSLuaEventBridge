import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import release_session as session


class ReleaseSessionTests(unittest.TestCase):
    def fixture(self, root):
        staged, installed = root/'stage', root/'installed'
        staged.mkdir(); installed.mkdir()
        (staged/'main.lua').write_text('new code')
        (staged/'enabled.txt').write_text('')
        (installed/'main.lua').write_text('old code')
        (installed/'enabled.txt').write_text('existing enablement marker')
        (installed/'config.ini').write_text('personal settings')
        manifest = {'module':'Example','version':'1.0.0','files':{
            p.name:session.digest(p) for p in staged.iterdir()}}
        return staged, installed, manifest

    def test_deployment_preserves_settings_enablement_and_backup(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            staged, installed, manifest = self.fixture(root)
            backup = session.deploy(manifest, staged, installed, root/'records', lambda:None)
            self.assertEqual((installed/'main.lua').read_text(), 'new code')
            self.assertEqual((installed/'config.ini').read_text(), 'personal settings')
            self.assertEqual((installed/'enabled.txt').read_text(), 'existing enablement marker')
            self.assertEqual((backup/'before/main.lua').read_text(), 'old code')
            self.assertIn('"status": "deployed"', (backup/'result.json').read_text())

    def test_deployment_never_creates_enablement_marker(self):
        for fresh in (False, True):
            with self.subTest(fresh=fresh), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                staged, installed, manifest = self.fixture(root)
                (installed/'enabled.txt').unlink()
                if fresh:
                    installed = root/'game/Mods/Example'
                session.deploy(manifest, staged, installed, root/'records', lambda:None)
                self.assertEqual((installed/'main.lua').read_text(), 'new code')
                self.assertFalse((installed/'enabled.txt').exists())
                if fresh:
                    self.assertEqual((root/'game/Mods/mods.txt').read_text(), 'Example : 1\n')
                else:
                    self.assertFalse((root/'mods.txt').exists())
                result = session.compare(manifest, staged, installed)
                self.assertFalse(result['needs_attention'])

    def test_fresh_deployment_preserves_explicit_disabled_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            staged, _, manifest = self.fixture(root)
            installed = root/'game/Mods/Example'
            mods = root/'game/Mods/mods.txt'
            mods.parent.mkdir(parents=True)
            original = b'Other : 1\r\nExample : 0\r\n'
            mods.write_bytes(original)
            session.deploy(manifest, staged, installed, root/'records', lambda:None)
            self.assertEqual(mods.read_bytes(), original)
            record = next((root/'records').glob('deploy-*/result.json'))
            self.assertIn('"status": "preserved-disabled"', record.read_text())

    def test_running_game_and_tampered_stage_never_deploy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            staged, installed, manifest = self.fixture(root)
            with self.assertRaises(RuntimeError):
                session.deploy(manifest, staged, installed, root/'records', lambda:{'pid':1})
            (staged/'main.lua').write_text('unexpected')
            with self.assertRaises(ValueError):
                session.deploy(manifest, staged, installed, root/'records', lambda:None)
            self.assertEqual((installed/'main.lua').read_text(), 'old code')
            self.assertFalse((root/'records').exists())

    def test_mid_deployment_start_records_incomplete_and_stops(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            staged, installed, manifest = self.fixture(root)
            calls = iter([None, {'pid':1}])
            with self.assertRaises(RuntimeError):
                session.deploy(manifest, staged, installed, root/'records', lambda:next(calls))
            self.assertEqual((installed/'main.lua').read_text(), 'old code')
            record = next((root/'records').glob('*/result.json'))
            self.assertIn('"status": "incomplete"', record.read_text())

    def test_activation_is_ordered_idempotent_and_preserves_foreign_entries(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            mods = root/'game/Mods/mods.txt'
            mods.parent.mkdir(parents=True)
            original = (b'; retained header\r\nOtherMod : 1\r\n'
                        b'UE4SSLuaEventBridge : 0 ; retained note\r\nKeybinds : 1\r\n')
            mods.write_bytes(original)
            backup = session.activate_mod(
                mods, 'UE4SSLuaEventBridge', root/'records', lambda:None)
            self.assertEqual(
                mods.read_bytes(),
                (b'; retained header\r\nUE4SSLuaEventBridge : 1 ; retained note\r\n'
                 b'OtherMod : 1\r\nKeybinds : 1\r\n'))
            self.assertEqual((backup/'mods.txt.before').read_bytes(), original)
            self.assertIn('"status": "activated"', (backup/'result.json').read_text())
            before_records = sorted((root/'records').iterdir())
            result = session.activate_mod(
                mods, 'UE4SSLuaEventBridge', root/'records', lambda:None)
            self.assertEqual(result['status'], 'already-active')
            self.assertEqual(sorted((root/'records').iterdir()), before_records)

    def test_activation_handles_empty_crlf_file_and_rejects_unsafe_state(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            mods = root/'game/Mods/mods.txt'
            mods.parent.mkdir(parents=True)
            mods.write_bytes(b'\r\n')
            session.activate_mod(mods, 'UE4SSLuaEventBridge', root/'records', lambda:None)
            self.assertEqual(mods.read_bytes(), b'\r\nUE4SSLuaEventBridge : 1\r\n')

            enabled_late = b'Other : 1\r\nUE4SSLuaEventBridge : 1\r\n'
            self.assertEqual(
                session.activated_mods_bytes(
                    enabled_late, 'UE4SSLuaEventBridge', preserve_disabled=True),
                b'UE4SSLuaEventBridge : 1\r\nOther : 1\r\n')

            duplicate = (b'UE4SSLuaEventBridge : 1\r\n'
                         b'ue4ssluaeventbridge : 0\r\n')
            mods.write_bytes(duplicate)
            with self.assertRaisesRegex(ValueError, 'duplicate entries'):
                session.activate_mod(mods, 'UE4SSLuaEventBridge', root/'records-duplicate', lambda:None)
            self.assertEqual(mods.read_bytes(), duplicate)

            with self.assertRaisesRegex(RuntimeError, 'Close the game'):
                session.activate_mod(mods, 'AnotherMod', root/'records-running', lambda:{'pid':1})
            self.assertEqual(mods.read_bytes(), duplicate)

    def test_activation_aborts_if_game_starts_before_replace(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            mods = root/'game/Mods/mods.txt'
            mods.parent.mkdir(parents=True)
            original = b'OtherMod : 1\r\n'
            mods.write_bytes(original)
            calls = iter([None, {'pid':1}])
            with self.assertRaisesRegex(RuntimeError, 'started during activation'):
                session.activate_mod(mods, 'UE4SSLuaEventBridge', root/'records', lambda:next(calls))
            self.assertEqual(mods.read_bytes(), original)
            record = next((root/'records').glob('*/result.json'))
            self.assertIn('"status": "incomplete"', record.read_text())

    def test_ignore_is_explicit_and_does_not_survive_next_launch(self):
        calls = []
        def runner(command, **kwargs):
            calls.append((command, kwargs))
        result = {'needs_attention':True}
        with self.assertRaises(RuntimeError):
            session.launch(result, ['game.exe'], session_provider=lambda:None, runner=runner)
        session.launch(result, ['game.exe','-arg'], True, lambda:None, runner)
        self.assertEqual(calls, [(['game.exe','-arg'], {'shell':False})])
        with self.assertRaises(RuntimeError):
            session.launch(result, ['game.exe'], session_provider=lambda:None, runner=runner)
        with self.assertRaises(RuntimeError):
            session.launch(result, ['game.exe'], True, lambda:{'pid':1}, runner)


if __name__ == '__main__':
    unittest.main()
