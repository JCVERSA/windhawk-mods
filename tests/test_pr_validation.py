"""Offline tests for .github/pr_validation.py.

Only the parts that need no network access are tested here: metadata
parsing, marker blocks, settings YAML, and callback signatures. The
validators that fetch live data (author records, SPDX list, deployed
catalog) are intentionally not exercised.

Run from the repository root:
    python -m unittest discover -s tests -v
Requires: PyYAML (see .github/requirements-validation.txt), Python 3.12
in CI (3.11 also works).
"""

import sys
import unittest
from io import StringIO
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / ".github"))

import pr_validation  # noqa: E402


class MetadataParsingTest(unittest.TestCase):
    def test_parses_properties_and_multi_values(self):
        source = StringIO(
            "// ==WindhawkMod==\n"
            "// @id              test-mod\n"
            "// @name            Test Mod\n"
            "// @include         explorer.exe\n"
            "// @include         dwm.exe\n"
            "// ==/WindhawkMod==\n"
        )
        properties, warnings = pr_validation.get_mod_file_metadata(source)
        self.assertEqual(warnings, 0)
        self.assertEqual(properties[("id", None)][0], "test-mod")
        self.assertEqual(
            properties[("include", None)][0], "explorer.exe\ndwm.exe\n"
        )

    def test_warns_on_unknown_parameter(self):
        source = StringIO(
            "// ==WindhawkMod==\n"
            "// @bogus           value\n"
            "// ==/WindhawkMod==\n"
        )
        _, warnings = pr_validation.get_mod_file_metadata(
            source, warn_callback=lambda line, msg: 1
        )
        self.assertEqual(warnings, 1)


class MarkerBlockTest(unittest.TestCase):
    def test_missing_required_readme_warns(self):
        warnings = pr_validation.validate_marker_block(
            Path("mods/x.wh.cpp"), "int main() {}",
            "WindhawkModReadme", "README", required=True,
        )
        self.assertGreaterEqual(warnings, 1)

    def test_valid_readme_passes(self):
        source = (
            "// ==WindhawkModReadme==\n"
            "/*\n"
            "# Hello\n"
            "*/\n"
            "// ==/WindhawkModReadme==\n"
        )
        warnings = pr_validation.validate_marker_block(
            Path("mods/x.wh.cpp"), source,
            "WindhawkModReadme", "README", required=True,
        )
        self.assertEqual(warnings, 0)

    def test_stray_extra_marker_warns(self):
        source = (
            "// ==WindhawkModReadme==\n"
            "/*\n"
            "# Hello\n"
            "*/\n"
            "// ==/WindhawkModReadme==\n"
            "// ==WindhawkModReadme==\n"
        )
        warnings = pr_validation.validate_marker_block(
            Path("mods/x.wh.cpp"), source,
            "WindhawkModReadme", "README", required=True,
        )
        self.assertGreaterEqual(warnings, 1)


class SettingsYamlTest(unittest.TestCase):
    def _validate(self, body: str) -> int:
        source = (
            "// ==WindhawkModSettings==\n"
            "/*\n"
            f"{body}\n"
            "*/\n"
            "// ==/WindhawkModSettings==\n"
        )
        return pr_validation.validate_settings_yaml(
            Path("mods/x.wh.cpp"), source
        )

    def test_valid_settings(self):
        self.assertEqual(
            self._validate("- MySetting: true\n  $name: My setting"), 0
        )

    def test_duplicate_key_is_an_error(self):
        self.assertEqual(
            self._validate("- MySetting: true\n  MySetting: false"), 1
        )

    def test_object_value_is_an_error(self):
        self.assertEqual(self._validate("- MySetting:\n    nested: 1"), 1)


class CallbackSignaturesTest(unittest.TestCase):
    def test_correct_signature_passes(self):
        warnings = pr_validation.validate_callback_signatures(
            Path("mods/x.wh.cpp"), "BOOL Wh_ModInit() { return TRUE; }"
        )
        self.assertEqual(warnings, 0)

    def test_wrong_signature_warns(self):
        warnings = pr_validation.validate_callback_signatures(
            Path("mods/x.wh.cpp"), "int Wh_ModInit() { return 0; }"
        )
        self.assertEqual(warnings, 1)


class DownloadAndExecuteTest(unittest.TestCase):
    def test_download_and_execute_combination_warns(self):
        source = "URLDownloadToFileW(nullptr, url, file, 0, nullptr);\nCreateProcessW(...);"
        warnings = pr_validation.validate_download_and_execute(
            Path("mods/x.wh.cpp"), source
        )
        self.assertEqual(warnings, 1)

    def test_download_only_does_not_warn(self):
        source = "auto h = InternetOpenW(agent, 0, nullptr, nullptr, 0);"
        warnings = pr_validation.validate_download_and_execute(
            Path("mods/x.wh.cpp"), source
        )
        self.assertEqual(warnings, 0)

    def test_execute_only_does_not_warn(self):
        source = "ShellExecuteW(nullptr, L\"open\", path, nullptr, nullptr, SW_SHOW);"
        warnings = pr_validation.validate_download_and_execute(
            Path("mods/x.wh.cpp"), source
        )
        self.assertEqual(warnings, 0)


class FetchConfigTest(unittest.TestCase):
    def test_fetch_has_timeout_and_retries(self):
        # Regression guard for the unbounded urlopen() call: a hung server
        # must not hang the validation job until the Actions timeout.
        self.assertGreater(pr_validation.FETCH_TIMEOUT_SECONDS, 0)
        self.assertGreater(pr_validation.FETCH_ATTEMPTS, 1)


if __name__ == "__main__":
    unittest.main()
