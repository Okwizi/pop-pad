# SPDX-License-Identifier: GPL-3.0-or-later
"""A public repository must not carry details of the machines it was developed on.

Paths and addresses end up in notes and scripts without anyone noticing. They belong in the
git-ignored files listed in CLAUDE.md.
"""

import re
import subprocess

import pytest

BINARY_SUFFIXES = {".png", ".jpg", ".jpeg", ".tga", ".zip", ".dll"}
PLACEHOLDER_NAMES = {"user", "you", "name", "username", "yourname"}
PUBLIC_EMAIL = re.compile(r"@users\.noreply\.github\.com$|^noreply@anthropic\.com$")

HOME_DIRECTORY = re.compile(r"(?:/home/|/Users/|[A-Za-z]:\\Users\\)([A-Za-z0-9_.-]+)")
PRIVATE_ADDRESS = re.compile(
    r"\b(?:10\.\d{1,3}|192\.168|172\.(?:1[6-9]|2\d|3[01])|100\.(?:6[4-9]|[7-9]\d|1[01]\d|12[0-7]))\.\d{1,3}\.\d{1,3}\b"
)
EMAIL_ADDRESS = re.compile(r"[\w.+-]+@[\w-]+(?:\.[\w-]+)+")


def problems(line):
    found = [m.group(0) for m in HOME_DIRECTORY.finditer(line) if m.group(1).lower() not in PLACEHOLDER_NAMES]
    found += [m.group(0) for m in PRIVATE_ADDRESS.finditer(line)]
    found += [m.group(0) for m in EMAIL_ADDRESS.finditer(line) if not PUBLIC_EMAIL.search(m.group(0))]
    return found


def tracked_text_files(root):
    try:
        listing = subprocess.run(["git", "ls-files", "-z"], cwd=root, check=True, capture_output=True).stdout
    except (OSError, subprocess.CalledProcessError):
        pytest.skip("not a git checkout")
    files = {}
    for name in filter(None, listing.decode().split("\0")):
        path = root / name
        if path.suffix.lower() in BINARY_SUFFIXES or not path.is_file():
            continue
        try:
            files[name] = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
    return files


def test_tracked_files_hold_no_machine_specific_details(root):
    hits = [
        f"{name}:{number}: {item}"
        for name, text in tracked_text_files(root).items()
        for number, line in enumerate(text.splitlines(), 1)
        for item in problems(line)
    ]
    assert not hits, "move these into a git-ignored file (see CLAUDE.md):\n" + "\n".join(hits)


def test_the_check_recognises_what_it_is_meant_to_catch():
    # Built piecewise so that this file does not trip its own scan.
    samples = [
        "/home/" + "alice/project",
        "/Users/" + "alice/project",
        "C:\\Users\\" + "alice\\poppad-dev",
        ".".join(["192", "168", "1", "20"]),
        ".".join(["100", "101", "2", "3"]),
        ".".join(["172", "16", "0", "9"]),
        "alice" + "@" + "example.org",
    ]
    for sample in samples:
        assert problems(f"host: {sample}"), sample


def test_the_check_leaves_placeholders_and_public_addresses_alone():
    samples = [
        "/home/" + "user/project",
        "C:\\Users\\" + "you\\poppad-dev",
        "%USERPROFILE%\\poppad-dev",
        "PC_HOST=" + "user" + "@" + "windows-pc",
        "12345+alice" + "@" + "users.noreply.github.com",
        "noreply" + "@" + "anthropic.com",
        "version 10.0.19045.1",
        ".".join(["8", "8", "8", "8"]),
        ".".join(["127", "0", "0", "1"]),
    ]
    for sample in samples:
        assert not problems(f"host: {sample}"), sample
