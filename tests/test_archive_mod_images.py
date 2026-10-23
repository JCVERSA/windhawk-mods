"""Offline tests for scripts/archive_mod_images.py.

Run from the repository root:
    python -m unittest discover -s tests -v
Requires: requests (see .github/requirements.txt).
"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "scripts"))

import archive_mod_images  # noqa: E402


class ImageUrlToPathTest(unittest.TestCase):
    def test_strips_scheme(self):
        self.assertEqual(
            archive_mod_images.image_url_to_path(
                "https://i.imgur.com/abc.png"
            ),
            "i.imgur.com/abc.png",
        )

    def test_rejects_non_https(self):
        with self.assertRaises(ValueError):
            archive_mod_images.image_url_to_path("http://i.imgur.com/abc.png")

    def test_rejects_path_traversal(self):
        with self.assertRaises(RuntimeError):
            archive_mod_images.image_url_to_path(
                "https://i.imgur.com/../secret.png"
            )


class FindImageUrlsTest(unittest.TestCase):
    def test_finds_supported_readme_images_only(self):
        source = (
            "// ==WindhawkModReadme==\n"
            "/*\n"
            "![a](https://i.imgur.com/abc.png)\n"
            "![b](https://example.com/not-archived.png)\n"
            "*/\n"
            "// ==/WindhawkModReadme==\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.wh.cpp"
            path.write_text(source, encoding="utf-8")
            urls = archive_mod_images.find_image_urls(path)
        self.assertEqual(urls, ["https://i.imgur.com/abc.png"])


class DownloadConfigTest(unittest.TestCase):
    def test_download_has_timeout_and_size_cap(self):
        # Regression guard: download_image() references these constants;
        # a missing definition would fail every deploy with NameError.
        self.assertGreater(archive_mod_images.DOWNLOAD_TIMEOUT_SECONDS, 0)
        self.assertGreater(archive_mod_images.MAX_IMAGE_BYTES, 0)


if __name__ == "__main__":
    unittest.main()
