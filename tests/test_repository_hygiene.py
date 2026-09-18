import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("hygiene", Path(__file__).resolve().parents[1] / "tools/repository_hygiene.py")
hygiene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hygiene)

class HygieneTests(unittest.TestCase):
    def test_personal_and_generated_paths(self):
        for name in ["config.ini", "Mods/Example/config.ini", "Install_Mod.bat", "dist/mod.zip", "tests/config.ini.before.bak", "build/main.dll", ".env.local", "reports/session.json", "dumps/objects.csv", "docs/AUDIT-2026-09-18.md", "logs/session.txt", "capture.dump", "internal/architecture.md", "CrashContext.runtime-xml"]:
            with self.subTest(name=name):
                self.assertTrue(hygiene.violations(name, b""))
        for name in ["mod_settings.ini", "Scripts/main.lua", "tools/build.ps1", "examples/config.example.ini", "tests/audit_regressions_test.lua"]:
            self.assertFalse(hygiene.violations(name, b""))

    def test_exception_does_not_allow_private_key(self):
        path = "distribution/config.ini"
        self.assertFalse(hygiene.violations(path, b"defaults", [path]))
        key = b"-----BEGIN " + b"PRIVATE KEY-----"
        self.assertTrue(hygiene.violations(path, key, [path]))

    def test_diagnostic_tools_and_evidence_are_private(self):
        for path in ['tools/runtime_evidence.py', 'tests/test_runtime_evidence.py',
                     'docs/RECONSTRUCTED_HISTORY.json', 'diagnostics/helper.py',
                     'evidence/session.json']:
            with self.subTest(path=path):
                self.assertTrue(hygiene.violations(path, b''))
        self.assertFalse(hygiene.violations('Scripts/main.lua', b''))
        self.assertFalse(hygiene.violations('release-manifest.json', b''))

    def test_index_and_deleted_historical_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            def git(*args):
                return hygiene.git(root, *args)
            git("init", "-q")
            git("config", "user.name", "Test")
            git("config", "user.email", "test@example.invalid")
            (root / "README.md").write_text("safe")
            git("add", ".")
            git("commit", "-qm", "baseline")
            base = git("rev-parse", "HEAD").decode().strip()
            (root / "config.ini").write_text("personal")
            git("add", "config.ini")
            (root / "config.ini").unlink()
            (root / "repository-policy.json").write_text('{"allowed_paths": ["config.ini"]}')
            # An unstaged policy exception must not change index interpretation.
            self.assertTrue(hygiene.check(root, [":index"]))
            git("commit", "-qm", "accidental config")
            git("add", "-u")
            git("commit", "-qm", "remove config")
            self.assertFalse(hygiene.check(root, ["HEAD"]))
            commits = git("rev-list", f"{base}..HEAD").decode().splitlines()
            self.assertTrue(hygiene.check(root, commits))
            # A second remote must not exempt unsafe history on a new public ref.
            head = git('rev-parse', 'HEAD').decode().strip()
            git('update-ref', 'refs/remotes/other/main', head)
            driver = Path(__file__).resolve().parents[1] / 'tools/check_incoming_history.py'
            import sys
            pushed = subprocess.run([sys.executable, str(driver), '--pre-push'],
                cwd=root, input=f'refs/heads/main {head} refs/heads/new-public {"0"*40}\n',
                text=True, capture_output=True)
            self.assertEqual(pushed.returncode, 1, pushed.stdout+pushed.stderr)
            self.assertIn('config.ini', pushed.stdout)

if __name__ == "__main__":
    unittest.main()
