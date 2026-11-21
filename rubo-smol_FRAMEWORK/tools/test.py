#!/usr/bin/env python3
"""
Run the tests that live on your computer.

    python3 tools/test.py

These do NOT need a Pico, and they do not need the SDK. Everything in tests/
is plain C++ that touches no hardware, so an ordinary compiler builds it and
runs it in a fraction of a second.

That is worth noticing. A bug you can catch here is one you never have to
chase with a robot driving into a table leg -- so when you write something
that is pure logic, like the JSON parser, keep it that way and test it here.

Add a file to tests/ and it is picked up automatically.
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TESTS = REPO / "tests"

FLAGS = ["-std=c++17", "-Wall", "-Wextra"]


def main():
    compiler = shutil.which("g++") or shutil.which("clang++")
    if compiler is None:
        sys.exit("No C++ compiler found. On Debian or Ubuntu: sudo apt-get install g++")

    sources = sorted(TESTS.glob("*.cpp"))
    if not sources:
        sys.exit(f"No tests found in {TESTS.relative_to(REPO)}/")

    failed = []

    with tempfile.TemporaryDirectory() as workspace:
        for source in sources:
            name = source.stem
            binary = Path(workspace) / name

            print(f"building {source.relative_to(REPO)} ...")
            compiled = subprocess.run([compiler, *FLAGS, "-o", str(binary), str(source)])
            if compiled.returncode != 0:
                failed.append(f"{name} (did not compile)")
                continue

            print(f"running  {name}")
            if subprocess.run([str(binary)]).returncode != 0:
                failed.append(name)

    print()
    if failed:
        for name in failed:
            print(f"  FAILED  {name}")
        sys.exit(1)

    print(f"  {len(sources)} test file(s) passed")


if __name__ == "__main__":
    main()
