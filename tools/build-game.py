#!/usr/bin/env python3
"""One locked tuning/compile/publication transaction per Make invocation.

Content fingerprints, not source mtimes, decide reuse. Every compiler consumes
one immutable generated header; candidate paths never enter game artifacts.
"""
import argparse
import decimal
from concurrent.futures import ThreadPoolExecutor
import fcntl
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time

# Import source bytes explicitly: timestamp/size-valid __pycache__ entries must
# never execute stale parser code while the fingerprint hashes a newer source.
TUNING_BYTES = Path(__file__).with_name("tuning.py").read_bytes()
_tuning = {"__name__": "tuning_source", "__file__": str(Path(__file__).with_name("tuning.py"))}
exec(compile(TUNING_BYTES, _tuning["__file__"], "exec"), _tuning)
for _name in ("Schema", "TuningError", "atomic", "digest", "packed", "readable", "unchanged", "writable"):
    globals()[_name] = _tuning[_name]


def env_words(name, default):
    return shlex.split(os.environ.get(name, default))


def identity(command):
    executable = shutil.which(command[0])
    if not executable:
        raise TuningError("compiler/tool unavailable: " + command[0])
    version = subprocess.check_output(command + ["--version"], stderr=subprocess.STDOUT)
    return {"command": command, "executable_sha256": digest(Path(executable).read_bytes()),
            "version": version.decode(errors="replace")}


def publish(updates):
    """Publish a recoverable rename group, then clean its owned scratch paths.

    Destination-local hard links retain old inodes for rollback. Signals are
    deferred over the short rename group. Cleanup after a durable publication
    is advisory; failed rollback retains its recovery copies. Uncatchable
    termination retains per-file atomicity only.
    """
    pending, directories = [], []
    committed, retain_backups = False, False
    try:
        for path, data, mode in updates:
            if path.exists() and path.read_bytes() == data:
                continue
            writable(path)
            local = Path(tempfile.mkdtemp(prefix=".tuning-publish-", dir=path.parent))
            directories.append(local)
            staged, backup = local / "new", local / "old"
            atomic(staged, data, mode)
            existed = path.exists()
            if existed:
                os.link(path, backup)
            pending.append((path, staged, backup, existed))
        previous = signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGINT, signal.SIGTERM})
        done = []
        try:
            for path, staged, backup, existed in pending:
                os.replace(staged, path)
                done.append((path, backup, existed))
            for directory in {path.parent for path, _, _, _ in pending}:
                fd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
                try:
                    os.fsync(fd)
                finally:
                    os.close(fd)
            committed = True
        except BaseException:
            for path, backup, existed in reversed(done):
                try:
                    if existed:
                        os.replace(backup, path)
                    else:
                        path.unlink()
                except OSError as error:
                    retain_backups = True
                    print("tuning rollback: could not restore " + str(path) + ": " + str(error), file=sys.stderr)
            raise
        finally:
            signal.pthread_sigmask(signal.SIG_SETMASK, previous)
    finally:
        for directory in directories:
            if retain_backups:
                print("tuning rollback: recovery files retained at " + str(directory), file=sys.stderr)
                continue
            errors = []
            # These two names are owned by this invocation. Explicit unlink
            # avoids recursive directory enumeration on shared filesystems.
            for name in ("new", "old"):
                try:
                    (directory / name).unlink(missing_ok=True)
                except OSError as error:
                    errors.append(str(error))
            try:
                directory.rmdir()
            except OSError as error:
                errors.append(str(error))
            if errors:
                state = "outputs published" if committed else "publication did not complete"
                print("tuning cleanup: " + state + "; temporary files retained at " +
                      str(directory) + ": " + "; ".join(errors), file=sys.stderr)


def source_hashes():
    return {str(path): digest(path.read_bytes()) for path in sorted(Path("code").rglob("*"))
            if path.is_file() and path.name != "tuning-defaults.json"
            and not any(part.startswith(".tuning-publish-") for part in path.parts)}


def artifact_spec(name):
    linux = env_words("LIN_CC", "gcc")
    windows = env_words("WIN_CC", "x86_64-w64-mingw32-gcc")
    libs = env_words("LIN_LIBS", "-lEGL -lGL -lX11 -lm")
    normal = "-std=c23 -O3 -ffast-math -funroll-loops -flto=auto -fno-tree-vectorize -Wall -Wextra -Wshadow"
    if name == "game":
        return linux, env_words("LIN_CFLAGS", normal), libs
    if name == "game-x86_64":
        return env_words("X86_CC", "gcc" if os.uname().machine == "x86_64" else "x86_64-linux-gnu-gcc"), env_words("LIN_CFLAGS", normal), libs
    if name == "game.exe":
        return windows, env_words("WIN_CFLAGS", "-std=c23 -O3 -ffast-math -funroll-loops -flto=auto -march=x86-64-v2 -Wall -Wextra -Wshadow -mwindows"), env_words("WIN_LIBS", "-static -lgdi32 -luser32 -lopengl32 -lwinmm -lole32 -lwinhttp -lws2_32 -lm")
    if name == "game-asan":
        return linux, env_words("SAN_CFLAGS", "-std=c23 -O1 -g -fsanitize=address,undefined,float-cast-overflow,float-divide-by-zero -Wall -Wextra -Wshadow"), libs
    if name in ("game-warning-linux.o", "game-warning-windows.o"):
        is_windows = "windows" in name
        flags = env_words("WARN_CFLAGS", "-std=c23 -O2 -Wall -Wextra -Wshadow -Werror")
        if is_windows:
            flags += ["-march=x86-64-v2"]
        return windows if is_windows else linux, flags + ["-c"], []
    if name == "game.res.o":
        return env_words("WIN_RES", "x86_64-w64-mingw32-windres"), ["--use-temp-file"], []
    raise TuningError("unknown game artifact: " + name)


def run(command):
    print(shlex.join(command), flush=True)
    result = subprocess.run(command, check=False)
    if result.returncode:
        raise TuningError("compiler exited " + str(result.returncode))


def transaction(args):
    inputs = source_hashes()
    schema = Schema("code/core/tuning-schema.json")
    canonical = Path("code/core/tuning-defaults.json")
    prior_canonical = readable(canonical)
    values = schema.canonical(canonical)
    explicit = args.source is not None or "TUNING_SOURCE" in os.environ
    source = args.source if args.source is not None else os.environ.get("TUNING_SOURCE", "build/config.cfg")
    if not source:
        raise TuningError("explicit tuning source is empty")
    values, selection, source_identity = schema.select(values, source, explicit)
    canonical_bytes = schema.document(values)
    if canonical.read_bytes() != canonical_bytes:
        writable(canonical)
    tuning_hash = schema.tuning_hash(values)
    snapshot = Path("build/tuning/snapshots") / (schema.hash + "-" + tuning_hash)
    snapshot.mkdir(parents=True, exist_ok=True)
    header = snapshot / "tuning.h"
    atomic(header, schema.header(values))
    # A compile uses relative content-addressed paths; no authoring path or clock
    # becomes part of generated C, the public manifest or the fingerprint.
    atomic(snapshot / "values.json", canonical_bytes)
    generator = {"tools/build-game.py": digest(Path("tools/build-game.py").read_bytes()),
                 "tools/tuning.py": digest(TUNING_BYTES)}
    version, commit = os.environ.get("BUILD_VERSION", "dev"), os.environ.get("BUILD_COMMIT", "unknown")
    stamps = ["-DBUILD_VERSION=" + json.dumps(version), "-DBUILD_COMMIT=" + json.dumps(commit)]
    environment = {k: os.environ[k] for k in ("CPATH", "C_INCLUDE_PATH", "LIBRARY_PATH", "COMPILER_PATH", "GCC_EXEC_PREFIX", "SOURCE_DATE_EPOCH") if k in os.environ}
    names = list(dict.fromkeys(Path(name).name for name in args.targets))
    if not names:
        raise TuningError("no requested game artifacts")
    plans = []
    for name in names:
        compiler, flags, libraries = artifact_spec(name)
        if name != "game.res.o":
            flags += env_words("CPPFLAGS", "") + ["-I" + str(snapshot)]
        fingerprint_input = {"schema": schema.hash, "tuning": tuning_hash, "sources": inputs,
            "generator": generator, "compiler": identity(compiler), "target": name,
            "flags": flags, "libraries": libraries, "stamps": stamps, "environment": environment,
            "header_sha256": digest(header.read_bytes())}
        if name != "game.res.o":
            fingerprint_input["compiler_target"] = subprocess.check_output(compiler + ["-dumpmachine"], text=True).strip()
        if name == "game.exe":
            fingerprint_input["resource_tool"] = identity(env_words("WIN_RES", "x86_64-w64-mingw32-windres"))
        fingerprint = digest(packed(fingerprint_input))
        output = Path("build") / name
        manifest = Path("build/tuning/artifacts") / (name + ".json")
        manifest.parent.mkdir(parents=True, exist_ok=True)
        try:
            old = json.loads(manifest.read_bytes()) if manifest.exists() else {}
        except (ValueError, OSError):
            old = {}
        same = not args.force and old.get("fingerprint") == fingerprint and output.is_file() and old.get("binary_sha256") == digest(output.read_bytes())
        if not same:
            writable(output)
            writable(manifest)
        plans.append({"name": name, "compiler": compiler, "flags": flags, "libraries": libraries,
                      "output": output, "manifest": manifest, "same": same,
                      "record": {"fingerprint": fingerprint, "input": fingerprint_input}})
    if source_identity:
        unchanged(source, *source_identity)
    with tempfile.TemporaryDirectory(prefix="invocation-", dir="build/tuning") as temporary:
        temporary = Path(temporary)

        def compile_one(plan):
            if plan["same"]:
                return
            name = plan["name"]
            target = temporary / name
            command = plan["compiler"] + plan["flags"] + stamps
            if name == "game.res.o":
                command += ["code/game.rc", "-o", str(target)]
            else:
                command += ["code/game.c"]
                if name == "game.exe":
                    resource = temporary / "game.exe.res.o"
                    run(env_words("WIN_RES", "x86_64-w64-mingw32-windres") + ["--use-temp-file"] + stamps + ["code/game.rc", "-o", str(resource)])
                    command += [str(resource)]
                command += ["-o", str(target)] + plan["libraries"]
            run(command)
            if not target.is_file() or not target.stat().st_size:
                raise TuningError("compiler produced no artifact")
            plan["record"]["binary_sha256"] = digest(target.read_bytes())

        # The lock covers all requested compiles and their final publication.
        # Parallelism is internal; competing Make invocations cannot mix tunes.
        with ThreadPoolExecutor(max_workers=min(len(plans), os.cpu_count() or 1)) as pool:
            futures = [pool.submit(compile_one, plan) for plan in plans]
            errors = []
            for future in futures:
                try:
                    future.result()
                except Exception as exc:
                    errors.append(exc)
            if errors:
                raise errors[0]
        if inputs != source_hashes() or header.read_bytes() != schema.header(values) or any(digest(Path(p).read_bytes()) != h for p, h in generator.items()):
            raise TuningError("build source changed during compilation; no output published")
        # Candidate replacement during compilation intentionally affects the next
        # invocation only. Every output here was compiled from the pinned header.
        unchanged(canonical, *prior_canonical)
        updates = [(canonical, canonical_bytes, 0o644)]
        for plan in plans:
            if plan["same"]:
                continue
            data = (temporary / plan["name"]).read_bytes()
            updates.append((plan["output"], data, 0o644 if plan["name"].endswith(".o") else 0o755))
            updates.append((plan["manifest"], packed(plan["record"]) + b"\n", 0o644))
        selection.update({"schema_sha256": schema.hash, "tuning_sha256": tuning_hash,
                          "artifacts": names, "rebuilt": [p["name"] for p in plans if not p["same"]]})
        updates.append((Path("build/tuning/last-selection.json"), packed(selection) + b"\n", 0o600))
        publish(updates)
    print("tuning: " + selection["selection"] + " " + tuning_hash +
          " rebuilt=" + ",".join(selection["rebuilt"]), flush=True)


def remove_build():
    """Remove build/ while the caller holds the repository build lock."""
    build = Path("build")
    # Host indexers can briefly retain files or an already empty folder.
    # Retry under the same lock; a persistent denial aborts the rebuild.
    for attempt in range(20):
        try:
            if build.is_symlink():
                build.unlink()
            elif build.exists():
                shutil.rmtree(build)
            break
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.25)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--rebuild", action="store_true",
                        help="delete all of build/ before compiling the requested targets")
    parser.add_argument("targets", nargs="*")
    args = parser.parse_args()
    if not args.targets:
        raise TuningError("no requested game artifacts")
    # Ordinary signals unwind temporary files; a killed compiler never publishes
    # its partial output. Subsequent invocations also ignore unfinished temp dirs.
    def stop(signum, frame):
        raise TuningError("build interrupted by signal " + str(signum))
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    # The lock survives deletion of build/. Waiters must always lock the same
    # inode, including a rebuild queued behind an ordinary compilation.
    with open(".build.lock", "a+b") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if args.rebuild:
            remove_build()
            print("tuning: build/ removed; rebuilding from scratch", flush=True)
        Path("build/tuning").mkdir(parents=True, exist_ok=True)
        transaction(args)


if __name__ == "__main__":
    try:
        main()
    except (TuningError, OSError, ValueError, KeyError, decimal.InvalidOperation) as error:
        print("tuning build: " + str(error), file=sys.stderr)
        sys.exit(1)
