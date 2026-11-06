#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Reconfigure Quilla's ``arch/cpu`` serial port for the USART1 Quilla.FR board.

The reconfiguration lives ENTIRELY inside the fork:

  * ``quilla/arch/cpu/arm/py32f040xb/`` - USART1 / PA9-PA10 / DMA1_Channel3
    in shellport.c/.h and quilla-qboot-hal.c;
  * ``quilla/user/py32f040xb/`` - the QUILLA_SHELLPORT_USART1 define in every
    CHECK / SEALED / SEC build target.

QBOOT builds reference ``arch/cpu`` directly, exactly as upstream does. No
external overlay directory is a build dependency. ``quilla/os/`` is untouched.

``tools/quilla-usart1.patch`` is the single, reviewable source of truth. It is
a normal git diff against upstream, so it can be inspected, re-applied after a
``git pull``, or reverted. Usage::

    python tools/configure_quilla_usart1.py            # apply (idempotent)
    python tools/configure_quilla_usart1.py --check    # report only
    python tools/configure_quilla_usart1.py --revert   # restore upstream files

Before pulling upstream, revert first; see README.md.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QUILLA = ROOT / "quilla"
PATCH = ROOT / "tools" / "quilla-usart1.patch"
BASELINE = ROOT / "tools" / ".quilla-usart1-baseline"
MACRO = "QUILLA_SHELLPORT_USART1"

ARCH_DIR = QUILLA / "arch" / "cpu" / "arm" / "py32f040xb"
ARCH_FILES = ("shellport.c", "shellport.h", "quilla-qboot-hal.c")
GCC_MAKEFILES = (
    QUILLA / "user" / "py32f040xb" / "EIDE" / "Makefile.qboot",
    QUILLA / "user" / "py32f040xb" / "EIDE" / "Makefile.qboot.sealed",
    QUILLA / "user" / "py32f040xb" / "EIDE" / "Makefile.qboot.sec",
)
KEIL_PROJECT = QUILLA / "user" / "py32f040xb" / "MDK-ARM" / "QBOOT.uvprojx"


def git(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", "-C", str(QUILLA), *args],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def head_sha() -> str:
    r = git("rev-parse", "HEAD")
    return r.stdout.strip() if r.returncode == 0 else ""


def patch_check(reverse: bool) -> bool:
    """True when the patch can be applied (or reverse-applied) cleanly."""
    args = ["apply", "--check"]
    if reverse:
        args.append("-R")
    args.append(str(PATCH))
    return git(*args).returncode == 0


def record_baseline() -> None:
    sha = head_sha()
    if sha:
        BASELINE.write_text(sha + "\n", encoding="utf-8")


def check() -> int:
    problems: list[str] = []
    if not QUILLA.is_dir():
        print("ERROR: quilla/ not found. Clone it first:", file=sys.stderr)
        print("  git clone -b develop git@github.com:JiChiRen/Quilla.git quilla",
              file=sys.stderr)
        return 2
    if not PATCH.exists():
        print("ERROR: missing %s" % PATCH, file=sys.stderr)
        return 2

    sha = head_sha()
    recorded = BASELINE.read_text(encoding="utf-8").strip() if BASELINE.exists() else ""
    print("upstream HEAD : %s" % (sha or "unknown"))
    print("applied at    : %s" % (recorded or "never"))
    if recorded and sha and recorded != sha:
        problems.append("upstream advanced since the last apply; revert, pull, then re-apply")

    applied = patch_check(reverse=True)
    print("arch/cpu patch: %s" % ("applied" if applied else "NOT applied"))
    if not applied:
        problems.append("the USART1 patch is not applied to arch/cpu")

    for name in ARCH_FILES:
        path = ARCH_DIR / name
        if not path.exists():
            problems.append("missing %s" % path)
        elif MACRO not in path.read_text(encoding="utf-8", errors="replace"):
            problems.append("%s has no USART1 switch" % name)
        else:
            print("arch/cpu OK  : %s" % name)

    for path in (*GCC_MAKEFILES, KEIL_PROJECT):
        if not path.exists():
            problems.append("missing %s" % path)
            continue
        n = path.read_text(encoding="utf-8", errors="replace").count(MACRO)
        want = 3 if path == KEIL_PROJECT else 1
        status = "OK" if n >= want else "MISSING"
        if n < want:
            problems.append("%s: %d define(s), expected %d" % (path.name, n, want))
        print("%-9s: %s (%d define(s))" % (status, path.name, n))

    if problems:
        print("\nNOT CONFIGURED:")
        for item in problems:
            print("  - " + item)
        return 1
    print("\nConfigured for USART1 / PA9-PA10 / DMA1_Channel3.")
    print("QBOOT builds read quilla/arch/cpu/arm/py32f040xb/ directly.")
    return 0


def apply() -> int:
    if not QUILLA.is_dir():
        print("ERROR: quilla/ not found; clone it first.", file=sys.stderr)
        return 2
    if not PATCH.exists():
        print("ERROR: missing %s" % PATCH, file=sys.stderr)
        return 2

    if patch_check(reverse=True):
        print("arch/cpu patch already applied; nothing to do.")
        record_baseline()
        return 0

    if not patch_check(reverse=False):
        print("ERROR: patch does not apply. Upstream changed arch/cpu or the build",
              file=sys.stderr)
        print("       files; review 'git -C quilla diff' and regenerate the patch.",
              file=sys.stderr)
        return 1

    r = git("apply", str(PATCH))
    if r.returncode != 0:
        print("ERROR: git apply failed: %s" % r.stderr.strip(), file=sys.stderr)
        return 1
    record_baseline()
    print("Applied to quilla/arch/cpu/arm/py32f040xb/ and all QBOOT build targets.")
    print("Build CHECK / SEALED / SEC, then run --check.")
    return 0


def revert() -> int:
    if not QUILLA.is_dir():
        print("ERROR: quilla/ not found.", file=sys.stderr)
        return 2
    if not PATCH.exists():
        print("ERROR: missing %s" % PATCH, file=sys.stderr)
        return 2

    if not patch_check(reverse=True):
        print("Nothing to revert: patch is not applied (or upstream drifted).")
        if BASELINE.exists():
            BASELINE.unlink()
        return 0
    r = git("apply", "-R", str(PATCH))
    if r.returncode != 0:
        print("ERROR: reverse apply failed: %s" % r.stderr.strip(), file=sys.stderr)
        return 1
    if BASELINE.exists():
        BASELINE.unlink()
    print("Restored upstream arch/cpu files and build defines.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    group = ap.add_mutually_exclusive_group()
    group.add_argument("--check", action="store_true", help="report only, change nothing")
    group.add_argument("--revert", action="store_true", help="restore upstream files")
    args = ap.parse_args()
    if args.check:
        return check()
    if args.revert:
        return revert()
    return apply()


if __name__ == "__main__":
    sys.exit(main())
