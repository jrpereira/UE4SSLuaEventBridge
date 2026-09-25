import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class VersionResourceTests(unittest.TestCase):
    def test_resource_and_packaging_share_canonical_version(self):
        header = (ROOT / 'contract/Version.hpp').read_text()
        match = re.search(r'^#define UE4SSLEB_VERSION "(\d+)\.(\d+)\.(\d+)"$', header, re.M)
        self.assertIsNotNone(match)
        major, minor, patch = match.groups()
        self.assertRegex(header, rf'(?m)^#define UE4SSLEB_VERSION_MAJOR {major}$')
        self.assertRegex(header, rf'(?m)^#define UE4SSLEB_VERSION_MINOR {minor}$')
        self.assertRegex(header, rf'(?m)^#define UE4SSLEB_VERSION_PATCH {patch}$')

        template = (ROOT / 'bridge/resources/Version.rc.in').read_text()
        rendered = (template
            .replace('@PROJECT_VERSION_MAJOR@', major)
            .replace('@PROJECT_VERSION_MINOR@', minor)
            .replace('@PROJECT_VERSION_PATCH@', patch)
            .replace('@UE4SSLEB_PRODUCT_VERSION@', '.'.join(match.groups())))
        self.assertIn(f'FILEVERSION {major},{minor},{patch},0', rendered)
        self.assertIn(f'VALUE "ProductVersion", "{major}.{minor}.{patch}\\0"', rendered)
        version = '.'.join(match.groups())
        self.assertIn(f'VALUE "OriginalFilename", "UE4SSLuaEventBridge-{version}.dll\\0"', rendered)

        bridge_cmake = (ROOT / 'bridge/CMakeLists.txt').read_text()
        bootstrap_cmake = (ROOT / 'bootstrap/CMakeLists.txt').read_text()
        self.assertIn('resources/Version.rc.in', bridge_cmake)
        self.assertIn('UE4SSLuaEventBridge-${UE4SSLEB_PRODUCT_VERSION}', bridge_cmake)
        self.assertIn('resources/Version.rc.in', bootstrap_cmake)

        selector = (ROOT / 'packaging/main.json.in').read_text()
        self.assertIn('"schema": 1', selector)
        self.assertIn('"version": "@UE4SSLEB_PRODUCT_VERSION@"', selector)

        packager = (ROOT / 'tools/package-release.ps1').read_text()
        self.assertIn("test-dll-version.ps1') -Dll $bootstrap", packager)
        self.assertIn("test-dll-version.ps1') -Dll $implementation", packager)

        for workflow in ['build.yml', 'release.yml']:
            source = (ROOT / '.github/workflows' / workflow).read_text()
            self.assertIn('test-dll-version.ps1', source)


if __name__ == '__main__':
    unittest.main()
