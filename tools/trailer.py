#!/usr/bin/env python3
"""Build the native game and regenerate the trailer, reporting total wall time."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", default="make")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    started = time.monotonic()
    status = 1
    try:
        # Compiler/profile variables are exported by the outer Makefile; -e keeps
        # them through the inner Makefile. Jobserver descriptors do not survive
        # subprocess, so this invocation uses a fresh Make job.
        env = {key: value for key, value in os.environ.items()
               if key not in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES")}
        subprocess.run(shlex.split(args.make) + ["--no-print-directory", "-e", "-B", "build/game"],
                       cwd=root, env=env, check=True)
        subprocess.run([sys.executable, "-u", "media/media.py", "all", "--fresh"],
                       cwd=root, env=env, check=True)
        status = 0
    except subprocess.CalledProcessError as error:
        status = error.returncode if error.returncode > 0 else 128 - error.returncode
    except KeyboardInterrupt:
        status = 130
    except OSError as error:
        print("trailer: " + str(error), file=sys.stderr)
    finally:
        elapsed = time.monotonic() - started
        hours, remainder = divmod(int(elapsed), 3600)
        minutes, seconds = divmod(remainder, 60)
        print("trailer: elapsed %02d:%02d:%02d (%.1f s), %s" %
              (hours, minutes, seconds, elapsed, "OK" if status == 0 else "FAILED"), flush=True)
    return status


if __name__ == "__main__":
    sys.exit(main())
