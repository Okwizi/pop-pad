# Notes for AI coding agents

For Claude Code and any other agent working in this repository. People contributing by hand will find
the same rules in [CONTRIBUTING.md](CONTRIBUTING.md); the README says what the project is for.

This repository is **public**. Never put anything personal or machine-specific in a tracked file:
host names, IP addresses, user names, home-directory paths, e-mail addresses, tokens. See
[Files that stay on your machine](#files-that-stay-on-your-machine).

## What this is

`dinput8.dll`, a proxy DLL that makes *Prince of Persia: The Two Thrones* (32-bit, Steam app 13530)
use its built-in Xbox controller code with any XInput pad, and draws controller button icons instead
of keyboard prompts. It is injected by being named like a system DLL and placed next to `POP3.EXE`.

## Everyday commands

```bash
tools/check.sh           # everything CI runs: format, lint, build with -Werror, unit tests
tools/check.sh --fix     # let clang-format and ruff rewrite files first
python3 tools/build_glyphs.py   # pack assets/glyphs/<style>/ into dist/poppad/glyphs/<style>.tga
tools/package.sh         # build the release zip into dist/
```

Setup is in the README under *Building from source*. The DLL is cross-compiled with MinGW-w64;
building on Windows has not been tried.

## Rules that are easy to get wrong

- **Branches.** Work on a feature branch and open a pull request into `develop`. Only `develop` goes
  into `master`. Both are protected, so a push to either is rejected. History is linear: pull requests
  are rebased. Commit messages are Conventional Commits (`feat(icons): ...`).
- **No game content.** Do not add game files, dumps, extracted textures or long disassembly. Tests build
  their own inputs from the formats written down in `src/font_file.h` and `src/font_patch.cpp`.
  An address or a short byte signature in a comment, with how it was found, is fine.
- **One address-dependent patch.** `src/brackets.cpp` is the only code tied to an exact `POP3.EXE`
  build. It compares the bytes it expects before touching anything and leaves the game alone on a
  mismatch. Any new patch must do the same.
- **Icon characters.** `ICON_CHARS` in `src/icons.cpp` must avoid the nine bytes the game uses for its
  own button placeholders (B3 B5 B6 B9 BA BB BC BD BE). The C++ and Python sides of the icon layout
  must agree; `tests/test_sources_agree.py` fails when they drift.
- **Settings.** Every key in `poppad.ini` is read by the DLL, and every key the DLL reads is in
  `poppad.ini` (a test checks both). The `[Dev]` keys are the exception and must never appear in the
  shipped file.
- **Comments say why**, not what. A value found by reverse engineering says how it was found.
- **Warnings are errors** in CI. Fix them; do not silence them.

## Testing against the real game

Unit tests cover the file formats. Anything else needs the game running on a Windows machine that
owns it. `tools/dev` is a remote rig for that, driven from a Linux or macOS machine over SSH:

```bash
cp tools/dev/local.env.example tools/dev/local.env   # then fill it in; git ignores it
tools/dev/deploy.sh devpad      # build, then copy the DLL and an ini to the game folder
tools/dev/launch.sh
tools/dev/send.sh 'tap START; sleep 1500; shot pause'
tools/dev/send.sh quit
```

With `[Dev] Commands=1` the DLL runs a script dropped next to the game as `poppad.cmd`, one command per
line, ending with `end`: `sleep`, `tap`, `hold`, `release`, `stick`, `key`, `shot <name>` and `quit`
(see `src/cmd.cpp`). It writes `poppad.done` when finished, and screenshots land in `poppad_shots\`.
`[Dev] VirtualPad=1` supplies a scripted pad, so no controller has to be connected. The deploy modes
are `user` (what a player gets), `dev` (adds the command channel) and `devpad` (adds the virtual
pad). Finish with a `user` deployment so nothing development-only is left on the machine.

**The game is someone's computer.** Take these seriously:

- **It takes the whole screen** (exclusive fullscreen). Do not launch it while a person is using that
  machine; ask first. The operating system cannot screenshot it, which is why `shot` captures inside
  the DLL.
- **Leave with `quit`, never by killing `POP3.EXE`.** A kill or a crash leaves a `PrinceSM` file in
  the game folder, and the next start stops at a "use safe mode settings?" dialog. Delete the file.
- **Do not create profiles or change display modes on an install that is not yours.** The last mode is
  stored in `POP3Profiles\DefaultProfile.DAT`; a mode the monitor refuses can stop the game starting.
  pop-pad already keeps `Profile.DAT.before-pop-pad` when it edits a profile. Back up the rest first.
- **A GUI program cannot be started from an SSH shell**, because the shell is not in the desktop
  session. `launch.sh` starts it through a one-off scheduled task for that reason.

### An agent running on the game machine itself

The scripts in `tools/dev` are bash and reach the machine with `ssh`/`scp` (see `common.sh`), so they
are not written for running on Windows, and that setup is **untested**. An agent on that machine can
do the same things with plain file operations in the game folder:

| To | Do |
|---|---|
| Install a build | copy `dinput8.dll` and `poppad.ini` next to `POP3.EXE` (plus `poppad\glyphs\*.tga`) |
| Run a script | write `poppad.cmd` ending in `end`, wait for `poppad.done` |
| See the screen | read `poppad_shots\<name>.bmp` after a `shot <name>` command |
| See what happened | read `poppad.log` (also used in bug reports) |
| Start the game | `steam://rungameid/13530`; `tools/dev/launch.ps1` does this and presses Enter on the launcher |

Build on a machine that has the MinGW-w64 toolchain and bring `dinput8.dll` over, unless you have got
a Windows build working.

## Files that stay on your machine

Git ignores these, so use them for anything that is yours alone. Do not move their contents into
tracked files, and do not paste them into issues or pull requests.

| File | For |
|---|---|
| `tools/dev/local.env` | the test machine's address and the game folder (copy of `local.env.example`) |
| `CLAUDE.local.md` | personal notes for your own agent sessions |
| `.claude/settings.local.json` | your Claude Code permissions and hooks |
| `backups/`, `shots/` | copies of your own game files, and screenshots from the rig |

`tests/test_repo_hygiene.py` fails if a tracked file contains a home-directory path or a private
network address. That is a backstop, not a substitute for looking at what you are about to commit.
