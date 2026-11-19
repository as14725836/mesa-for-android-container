# SPDX-License-Identifier: MIT
"""Exercise the workflow's actual release publisher with an offline GitHub API."""

import contextlib
import io
import json
import os
from pathlib import Path
import sys
import textwrap
import unittest
from unittest import mock
import urllib.error


WORKFLOW = Path(__file__).resolve().parents[1] / "workflows/turnip-build.yml"


def publisher_code():
    # Compile the actual heredoc so the test cannot diverge from the workflow.
    script = WORKFLOW.read_text(encoding="utf-8").split("<<'PYEOF'\n", 1)[1]
    return compile(textwrap.dedent(script.split("          PYEOF", 1)[0]),
                   str(WORKFLOW), "exec")


class ReleasePublishTests(unittest.TestCase):
    def run_publisher(self, fail_at=None):
        calls = []

        def request(req):
            calls.append(req)
            if len(calls) - 1 == fail_at:
                raise urllib.error.HTTPError(req.full_url, 422, "Rejected", {}, None)
            data = {"id": 42, "upload_url": "https://uploads.example/assets{?name}",
                    "html_url": "https://example/release"}
            return io.BytesIO(json.dumps(data).encode())

        def open_file(path, mode="r", **kwargs):
            return io.BytesIO(b"asset") if "b" in mode else io.StringIO("notes")

        env = {"GH_TOKEN": "test-token", "GITHUB_REPOSITORY": "test/repo",
               "GIT_HASH": "a" * 40}
        with (mock.patch.dict(os.environ, env),
              mock.patch.object(sys, "argv", ["-", "V21", "mesa.tar.gz", "notes.md"]),
              mock.patch("urllib.request.urlopen", side_effect=request),
              contextlib.redirect_stdout(io.StringIO())):
            code = publisher_code()
            with mock.patch("builtins.open", side_effect=open_file):
                if fail_at is None:
                    exec(code, {})
                else:
                    with self.assertRaises(urllib.error.HTTPError):
                        exec(code, {})
        return calls

    def test_reserve_exact_commit_upload_both_assets_then_publish(self):
        calls = self.run_publisher()
        self.assertEqual([c.method for c in calls],
                         ["POST", "POST", "POST", "POST", "PATCH"])
        self.assertTrue(calls[0].full_url.endswith("/git/refs"))
        self.assertEqual(json.loads(calls[0].data),
                         {"ref": "refs/tags/V21", "sha": "a" * 40})
        draft = json.loads(calls[1].data)
        self.assertTrue(draft["draft"])
        self.assertEqual(draft["target_commitish"], "a" * 40)
        self.assertTrue(calls[2].full_url.endswith("?name=mesa.tar.gz"))
        self.assertTrue(calls[3].full_url.endswith("?name=mesa.tar.gz.sha256"))
        self.assertTrue(calls[4].full_url.endswith("/releases/42"))
        self.assertEqual(json.loads(calls[4].data), {"draft": False})

    def test_tag_conflict_or_upload_failure_never_publishes(self):
        for fail_at in range(4):
            with self.subTest(fail_at=fail_at):
                calls = self.run_publisher(fail_at=fail_at)
                self.assertEqual(len(calls), fail_at + 1)
                self.assertNotIn("PATCH", [c.method for c in calls])

    def test_publication_failure_is_not_silenced_or_retried(self):
        calls = self.run_publisher(fail_at=4)
        self.assertEqual(len(calls), 5)


if __name__ == "__main__":
    unittest.main()
