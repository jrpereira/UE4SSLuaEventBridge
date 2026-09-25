import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class VersionResourceTests(unittest.TestCase):
    def test_resource_and_packaging_share_canonical_version(self):
        header = (ROOT / 'mod/include/UE4SSLuaEventBridge/Version.hpp').read_text()
        match = re.search(r'^#define UE4SSLEB_VERSION "(\d+)\.(\d+)\.(\d+)"$', header, re.M)
        self.assertIsNotNone(match)
        major, minor, patch = match.groups()

        template = (ROOT / 'cmake/Version.rc.in').read_text()
        rendered = (template
            .replace('@PROJECT_VERSION_MAJOR@', major)
            .replace('@PROJECT_VERSION_MINOR@', minor)
            .replace('@PROJECT_VERSION_PATCH@', patch)
            .replace('@UE4SSLEB_PRODUCT_VERSION@', '.'.join(match.groups())))
        self.assertIn(f'FILEVERSION {major},{minor},{patch},0', rendered)
        self.assertIn(f'VALUE "ProductVersion", "{major}.{minor}.{patch}\\0"', rendered)
        self.assertIn('VALUE "OriginalFilename", "main.dll\\0"', rendered)

        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('cmake/Version.rc.in', cmake)
        self.assertIn('"${UE4SSLEB_VERSION_RESOURCE}"', cmake)

        packager = (ROOT / 'tools/package-release.ps1').read_text()
        self.assertIn("test-dll-version.ps1') -Dll $dll -ExpectedVersion $metadata.version", packager)

        for workflow in ['build.yml', 'release.yml']:
            source = (ROOT / '.github/workflows' / workflow).read_text()
            self.assertIn('test-dll-version.ps1', source)


if __name__ == '__main__':
    unittest.main()
