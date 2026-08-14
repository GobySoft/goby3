#!/usr/bin/env python3
"""Checks that the Julia and Python generators agree on the interface.yml format.

The two generators are separate implementations in separate languages, so nothing but a test
stops the shared format from drifting into two dialects. This runs both over the shared
conformance corpus and fails if they disagree about any file.
"""

import argparse
import os
import subprocess
import sys
import tempfile

from goby import gen

JULIA_DRIVER = """
include("{gen_goby}")
try
    mktempdir() do dir
        goby_gen_cpp("{interface}", joinpath(dir, "out.cpp"), [])
    end
    exit(0)
catch e
    println(stderr, e)
    exit(1)
end
"""


def julia_accepts(julia, goby_jl, interface):
    driver = JULIA_DRIVER.format(
        gen_goby=os.path.join(goby_jl, "src", "gen_goby.jl"), interface=interface
    )
    with tempfile.NamedTemporaryFile("w", suffix=".jl", delete=False) as handle:
        handle.write(driver)
        script = handle.name
    try:
        result = subprocess.run(
            [julia, f"--project={goby_jl}", script],
            capture_output=True,
            text=True,
        )
        return result.returncode == 0, (result.stderr or result.stdout).strip()
    finally:
        os.unlink(script)


def python_accepts(interface):
    try:
        gen.load(interface)
        return True, ""
    except gen.InterfaceError as error:
        return False, str(error)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--julia", required=True, help="path to the julia executable")
    parser.add_argument("--goby-jl", required=True, help="path to the Goby.jl package")
    parser.add_argument("--corpus", required=True, help="path to the conformance corpus")
    args = parser.parse_args(argv)

    failures = []

    for expected_accept, subdirectory in ((True, "valid"), (False, "invalid")):
        directory = os.path.join(args.corpus, subdirectory)
        for name in sorted(os.listdir(directory)):
            interface = os.path.join(directory, name)

            python_ok, python_message = python_accepts(interface)
            julia_ok, julia_message = julia_accepts(args.julia, args.goby_jl, interface)

            for language, accepted, message in (
                ("python", python_ok, python_message),
                ("julia", julia_ok, julia_message),
            ):
                if accepted != expected_accept:
                    verb = "rejected" if expected_accept else "accepted"
                    failures.append(
                        f"{subdirectory}/{name}: the {language} generator {verb} it"
                        + (f": {message}" if message else "")
                    )

            status = "accept" if expected_accept else "reject"
            print(f"{subdirectory}/{name}: both generators {status} (as expected)")

    if failures:
        print("\nThe generators disagree with the shared format:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
