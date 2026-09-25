import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class WorkflowRuntimeTests(unittest.TestCase):
    def test_actions_use_node24_compatible_checkout_and_native_setup(self):
        workflows = list((ROOT / '.github/workflows').glob('*.yml'))
        combined = '\n'.join(path.read_text() for path in workflows)
        self.assertNotIn('actions/checkout@v4', combined)
        self.assertNotIn('actions/setup-python@v5', combined)
        self.assertNotIn('ilammy/msvc-dev-cmd', combined)
        self.assertIn('actions/checkout@v6', combined)
        self.assertIn('actions/setup-python@v6', combined)

        native_script = (ROOT / 'tools/ci-windows-native.ps1').read_text()
        self.assertIn('vswhere.exe', native_script)
        self.assertIn('Microsoft.VisualStudio.Component.VC.Tools.x86.x64', native_script)
        self.assertIn('Microsoft.VisualStudio.DevShell.dll', native_script)


if __name__ == '__main__':
    unittest.main()
