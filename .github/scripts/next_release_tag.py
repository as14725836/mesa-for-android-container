#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Allocate the next Turnip version from all existing remote Git tags.

Run under the release workflow's turnip-release concurrency group. Tags without
GitHub Releases still reserve their version. A failed remote query must abort
rather than fall back to an empty tag list or a stale local checkout.
"""

import re
import subprocess


PREFIX = "xmobox-turnip-a8xx-gen8-V"
TAG_PATTERN = re.compile(re.escape(PREFIX) + r"([0-9]+)")


def next_tag(tags):
    numbers = [int(match.group(1)) for tag in tags
               if (match := TAG_PATTERN.fullmatch(tag))]
    return f"{PREFIX}{max(numbers, default=0) + 1}"


def main():
    result = subprocess.run(
        ["git", "ls-remote", "--tags", "--refs", "origin",
         f"refs/tags/{PREFIX}*"],
        check=True, capture_output=True, text=True, timeout=60,
    )
    tags = []
    for line in result.stdout.splitlines():
        _oid, ref = line.split()
        if not ref.startswith("refs/tags/"):
            raise ValueError(f"Unexpected tag reference: {ref}")
        tags.append(ref.removeprefix("refs/tags/"))
    print(f"TAG={next_tag(tags)}")


if __name__ == "__main__":
    main()
