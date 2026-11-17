# Contributing to pop-pad

Thank you for helping. This file covers how changes get in; the README covers what the project is.

## Branches

```
feature/<what>  --pull request-->  develop  --pull request-->  master  --tag vX.Y.Z-->  release
```

- **`develop`** is the default branch. It only takes pull requests from feature branches
  (`feature/<what>`, `fix/<what>`, `docs/<what>` and so on), never directly from `master`.
- **`master`** only takes pull requests from `develop`, and is what releases are cut from.
- Neither can be pushed to, force-pushed or deleted. Both need the CI checks to pass.
- History is linear: pull requests are **rebased** onto the target. There are no merge commits and
  no squashes, so keep the commits on your branch tidy: one logical change each, each one building.

These rules are enforced by repository rulesets and by the *Branch flow* check, which fails a pull
request opened from the wrong branch. From a fork, use a branch that is not called `develop` or
`master`.

## Before you open a pull request

```bash
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements-dev.txt
tools/check.sh          # formatting, lint, build, unit tests; CI runs the same steps
tools/check.sh --fix    # let clang-format and ruff rewrite files first
```

You need `cmake`, `ninja-build`, `g++`, `g++-mingw-w64-i686` and `shellcheck`
(`sudo apt install ...` on Debian or Ubuntu).

- **Tests.** Add a test for the behaviour you change when it can be tested without the game. The C++
  tests are in `tests/native` and build their inputs themselves; the Python tests are in `tests`.
  A change that only the running game can show should say how you tried it.
- **Formatting.** `clang-format` and `ruff` decide; do not argue style in review.
- **Comments** explain why, not what. If a value comes from reverse engineering the game, say how it
  was found, so the next person can check it.
- **Warnings are errors** in CI. Fix them rather than silencing them, unless the compiler is wrong
  and a comment says why.

## Commit messages

[Conventional Commits](https://www.conventionalcommits.org), in the imperative:

```
feat(icons): draw the Switch set at the same size as the others
fix(profiles): keep the backup when the edit fails
docs: explain the custom icon format
```

Types in use: `feat`, `fix`, `docs`, `test`, `refactor`, `build`, `ci`, `chore`.

## What not to contribute

Game files, dumps, extracted assets, or anything else copyrighted by others. The tests build their
own inputs for exactly this reason. Icon artwork must be CC0 or compatible with GPL-3.0-or-later,
with its source recorded in `assets/glyphs/SOURCE.md`.

## Releases

A release is a tag `vX.Y.Z` on a commit that is on `master`. Pushing it runs the Release workflow,
which repeats every check, builds the zip with `tools/package.sh` and publishes it. A tag with a
suffix (`v0.2.0-rc1`) is published as a pre-release.

GitHub has no fast-forward merge button; *Rebase and merge* is its nearest equivalent, and it writes
new commits. That is harmless for feature branches into `develop`. For `develop` into `master` it
means the two branches hold the same changes under different commit hashes, so check the pull
request's file list at each release and, if GitHub reports conflicts on changes that are already
released, ask the maintainer to fast-forward `master` to `develop` by hand.

## Reporting security problems

pop-pad runs inside the game's process and edits files in the game folder. If you find a way it
could do harm beyond that, please open a private security advisory on the repository rather than a
public issue.
