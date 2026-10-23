"""Offline tests for .github/preprocessor.py and extract_mod_symbols.py.

Run from the repository root:
    python -m unittest discover -s tests -v
Requires Python 3.11+ (StrEnum) — CI uses the pinned 3.12.
"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / ".github"))

from preprocessor import (  # noqa: E402
    blank_comments,
    expand_macros,
    get_string_literals,
    preprocess_conditionals,
    split_top_level,
)


class BlankCommentsTest(unittest.TestCase):
    def test_preserves_offsets_and_line_numbers(self):
        code = 'a /* comment\nmore */ b // tail\nc'
        blanked = blank_comments(code)
        self.assertEqual(len(blanked), len(code))
        self.assertEqual(blanked.count("\n"), code.count("\n"))
        self.assertNotIn("comment", blanked)
        self.assertTrue(blanked.endswith("c"))

    def test_comment_markers_inside_strings_are_kept(self):
        code = 'const char* s = "// not a comment";'
        self.assertIn("// not a comment", blank_comments(code))


class StringLiteralsTest(unittest.TestCase):
    def test_get_string_literals(self):
        self.assertEqual(
            get_string_literals('L"one" "two"'), ["one", "two"]
        )

    def test_split_top_level_ignores_commas_in_literals(self):
        self.assertEqual(
            split_top_level('a, "b,c", d', ","), ["a", ' "b,c"', " d"]
        )


class PreprocessConditionalsTest(unittest.TestCase):
    def test_resolved_branch_is_kept_other_is_blanked(self):
        source = "#if defined(_WIN64)\nkept\n#else\nremoved\n#endif\n"
        result = preprocess_conditionals(source, {"_WIN64": "1"})
        self.assertIn("kept", result.source)
        self.assertNotIn("removed", result.source)
        # Line numbers are preserved.
        self.assertEqual(result.source.count("\n"), source.count("\n"))
        self.assertEqual(result.unresolved_lines, {})

    def test_unresolved_condition_is_reported_not_guessed(self):
        source = "#if defined(SOME_HEADER_MACRO)\nmaybe\n#endif\n"
        result = preprocess_conditionals(source, {})
        self.assertNotEqual(result.unresolved_lines, {})
        # The governed line is left in place for the caller to reject.
        self.assertIn("maybe", result.source)


class ExpandMacrosTest(unittest.TestCase):
    def test_object_like_macro(self):
        result = expand_macros('f(NAME)', {"NAME": '"value"'})
        self.assertEqual(result, 'f("value")')

    def test_literals_are_not_expanded(self):
        result = expand_macros('"NAME"', {"NAME": '"value"'})
        self.assertEqual(result, '"NAME"')


class ExtractModSymbolsTest(unittest.TestCase):
    MOD_SOURCE = """// ==WindhawkMod==
// @id              test-mod
// @version         1.0
// @architecture    x86-64
// ==/WindhawkMod==

WindhawkUtils::SYMBOL_HOOK user32DllHooks[] = {
    {
        {L"CreateWindowExW"},
        (void*)CreateWindowExW_Hook,
    },
};
"""

    def test_extracts_symbols_per_arch(self):
        from extract_mod_symbols import Architecture, get_mod_symbols

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test-mod.wh.cpp"
            path.write_text(self.MOD_SOURCE, encoding="utf-8")
            result = get_mod_symbols(path, [])

        self.assertEqual(
            result[Architecture.amd64]["user32.dll"], ["CreateWindowExW"]
        )
        self.assertEqual(
            result[Architecture.arm64]["user32.dll"], ["CreateWindowExW"]
        )
        self.assertNotIn(Architecture.x86, result)

    def test_unmatched_patch_raises_instead_of_silent_noop(self):
        from extract_mod_symbols import get_mod_symbols

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test-mod.wh.cpp"
            path.write_text(self.MOD_SOURCE, encoding="utf-8")
            with self.assertRaises(Exception):
                get_mod_symbols(path, [(r"pattern-that-matches-nothing", "x")])


if __name__ == "__main__":
    unittest.main()
