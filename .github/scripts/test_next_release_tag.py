# SPDX-License-Identifier: MIT
"""Run with: python3 -m unittest discover -s .github/scripts -v"""

import contextlib
import io
import subprocess
import unittest
from unittest import mock

import next_release_tag as release


class NextReleaseTagTests(unittest.TestCase):
    def test_first_release(self):
        self.assertEqual(release.next_tag([]), release.PREFIX + "1")

    def test_numeric_order_and_unreleased_tags_reserve_versions(self):
        # V20 may have no associated GitHub Release; it still cannot be reused.
        tags = [release.PREFIX + v for v in ("9", "20", "10", "2")]
        self.assertEqual(release.next_tag(tags), release.PREFIX + "21")

    def test_unrelated_and_non_numeric_tags_are_ignored(self):
        tags = ["v99", release.PREFIX, release.PREFIX + "-1",
                release.PREFIX + "100-rc1", release.PREFIX + "9^{}",
                release.PREFIX + "1.0", release.PREFIX + "999\n",
                release.PREFIX + "2"]
        self.assertEqual(release.next_tag(tags), release.PREFIX + "3")

    def test_leading_zero_versions_are_numeric(self):
        self.assertEqual(release.next_tag([release.PREFIX + "009"]),
                         release.PREFIX + "10")

    def test_remote_tags_are_queried_instead_of_local_tags(self):
        output = (f"{'a' * 40}\trefs/tags/{release.PREFIX}10\n"
                  f"{'b' * 40}\trefs/tags/{release.PREFIX}20\n")
        with mock.patch.object(release.subprocess, "run") as run:
            run.return_value.stdout = output
            with contextlib.redirect_stdout(io.StringIO()) as stdout:
                release.main()
        self.assertEqual(stdout.getvalue(), f"TAG={release.PREFIX}21\n")
        run.assert_called_once_with(
            ["git", "ls-remote", "--tags", "--refs", "origin",
             f"refs/tags/{release.PREFIX}*"],
            check=True, capture_output=True, text=True, timeout=60,
        )

    def test_empty_remote_starts_at_one(self):
        with mock.patch.object(release.subprocess, "run") as run:
            run.return_value.stdout = ""
            with contextlib.redirect_stdout(io.StringIO()) as stdout:
                release.main()
        self.assertEqual(stdout.getvalue(), f"TAG={release.PREFIX}1\n")

    def test_failed_or_timed_out_query_does_not_allocate_a_version(self):
        errors = [subprocess.CalledProcessError(128, "git"),
                  subprocess.TimeoutExpired("git", 60)]
        for error in errors:
            with self.subTest(error=type(error).__name__):
                with mock.patch.object(release.subprocess, "run", side_effect=error):
                    with contextlib.redirect_stdout(io.StringIO()) as stdout:
                        with self.assertRaises(type(error)):
                            release.main()
                self.assertEqual(stdout.getvalue(), "")

    def test_malformed_remote_response_does_not_allocate_a_version(self):
        for output in ("invalid line extra\n", "hash refs/heads/main\n"):
            with self.subTest(output=output):
                with mock.patch.object(release.subprocess, "run") as run:
                    run.return_value.stdout = output
                    with contextlib.redirect_stdout(io.StringIO()) as stdout:
                        with self.assertRaises(ValueError):
                            release.main()
                self.assertEqual(stdout.getvalue(), "")


if __name__ == "__main__":
    unittest.main()
