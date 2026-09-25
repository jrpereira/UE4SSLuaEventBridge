import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class VersionResourceTests(unittest.TestCase):
    def test_embedded_lua_literals_stay_within_msvc_limit(self):
        source = (ROOT / 'bridge/lua/bridge_api.lua').read_text()
        cmake = (ROOT / 'bridge/CMakeLists.txt').read_text()
        template = (ROOT / 'bridge/cmake/EmbeddedLuaAPI.hpp.in').read_text()
        declarations = re.findall(
            r'string\(SUBSTRING "\$\{UE4SSLEB_LUA_API\}" (\d+) (-?\d+) (UE4SSLEB_LUA_API_\d+)\)',
            cmake)
        self.assertTrue(declarations)

        rebuilt = ''
        expected_offset = 0
        for offset_text, length_text, name in declarations:
            offset = int(offset_text)
            length = int(length_text)
            self.assertEqual(offset, expected_offset)
            chunk = source[offset:] if length == -1 else source[offset:offset + length]
            self.assertLessEqual(len(chunk), 7000)
            self.assertEqual(template.count(f'@{name}@'), 1)
            rebuilt += chunk
            expected_offset += len(chunk)
        self.assertEqual(rebuilt, source)

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
