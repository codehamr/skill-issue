#!/usr/bin/env python3
"""Deterministic host promotion/transaction controls in isolated scratch trees.

Compiler shims make race/error states reachable; real game compilation and the
in-binary defaults proof remain separate integration gates.
"""
import argparse
import contextlib
import decimal
import errno
import io
import types
import json
import os
import py_compile
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
from unittest.mock import patch

_tuning_path = Path(__file__).with_name("tuning.py")
_tuning = {"__name__": "tuning_gate_source", "__file__": str(_tuning_path)}
exec(compile(_tuning_path.read_bytes(), str(_tuning_path), "exec"), _tuning)
for _name in ("Schema", "TuningError", "atomic", "digest", "f32"):
    globals()[_name] = _tuning[_name]


SHIM = '''#!/usr/bin/env python3
import json,os,pathlib,re,sys,time
if '--version' in sys.argv: print('deterministic compiler control 1');sys.exit(0)
if '-dumpmachine' in sys.argv: print('test-target');sys.exit(0)
control=pathlib.Path('control.json')
cfg=json.loads(control.read_text()) if control.exists() else {}
if cfg.get('fail'):sys.exit(17)
if cfg.get('replace'):
 p=pathlib.Path(cfg['replace']);tmp=p.with_suffix('.replacement.'+str(os.getpid()));tmp.write_text(cfg['text']);tmp.replace(p)
if cfg.get('barrier'):
 pathlib.Path(cfg['barrier']+'.ready').write_text('ready')
 while not pathlib.Path(cfg['barrier']+'.go').exists():time.sleep(.01)
header=next((x[2:]+'/tuning.h' for x in sys.argv if x.startswith('-Ibuild/tuning/')),None)
value=re.search(r'#define TUNING_SHA "([a-f0-9]+)"',pathlib.Path(header).read_text())[1] if header else 'resource'
out=pathlib.Path(sys.argv[sys.argv.index('-o')+1]);out.write_text(value+'\\n')
'''


def build_module(name):
    path = Path("tools/build-game.py").resolve()
    module = types.ModuleType(name)
    module.__file__ = str(path)
    exec(compile(path.read_bytes(), str(path), "exec"), module.__dict__)
    return module


class Gate:
    def __init__(self, out):
        self.out = out
        self.rows = []
        self.schema = Schema("code/core/tuning-schema.json")
        self.base = self.schema.canonical("code/core/tuning-defaults.json")

    def check(self, name, condition):
        self.rows.append({"fixture": name, "ok": bool(condition)})
        if not condition:
            raise AssertionError("defaults-gate FAIL fixture=" + name)

    def source(self, text, explicit=True, schema=None):
        source = self.out / "parse.cfg"
        source.write_bytes(text.encode())
        before = source.read_bytes()
        try:
            return (schema or self.schema).select(self.base, source, explicit)[0]
        finally:
            self.check("parser-input-unchanged", source.read_bytes() == before)

    def rejects(self, name, text, explicit=True):
        try:
            self.source(text, explicit)
        except TuningError:
            self.check(name, True)
        else:
            self.check(name, False)

    def parser_controls(self):
        self.check("canonical-count", len(self.base) == 24)
        self.check("missing-keys-inherit", self.source("devmode 1\n") == self.base)
        for key, row in self.schema.rows.items():
            for bound in ("lo", "hi"):
                value = row[bound]
                selected = self.source("devmode 1\r\n" + key + " " + value.replace(".", ",") + "\r\n")
                self.check("all-keys-bound-" + key + "-" + bound,
                           float(selected[key]) == float(format(f32(decimal.Decimal(value)), ".9g")))
                canonical = self.out / "bound-canonical.json"
                canonical.write_bytes(self.schema.document(selected))
                self.check("bound-canonical-reread-" + key + "-" + bound,
                           self.schema.canonical(canonical) == selected)
        self.check("identical-duplicates", self.source("devmode 1\ndevmode 1\nar_recoil_up 0,30\nar_recoil_up 3e-1 # same\n")["ar_recoil_up"] == self.base["ar_recoil_up"])
        for name, text in {
            "marker-garbage": "devmode 1tail", "marker-range": "devmode 2",
            "marker-conflict": "devmode 1\ndevmode 0", "marker-empty": "devmode",
            "marker-glued-comment": "devmode 1#bad", "explicit-disabled": "devmode 0",
            "explicit-unmarked": "mv_run_speed 7", "duplicate-conflict": "devmode 1\nmv_run_speed 7\nmv_run_speed 8",
            "decimal-conflict-before-round": "devmode 1\nar_recoil_up .3000000001\nar_recoil_up .3000000002",
        }.items():
            self.rejects(name, text)
        for token in ("nan", "inf", "-inf", "7oops", "1e999999999999999999", "7 8", "", "0x1p2", "14.1", "1.99"):
            self.rejects("invalid-number-" + token, "devmode 1\nmv_run_speed " + token)
        excluded = "player_name $(touch BAD)\nmp_host `touch BAD`\ntelemetry nan\nvolume bad\npad_sens 99\n"
        retired = "".join(key + " nonsense\n" for key in self.schema.retired)
        self.check("excluded-retired-values", self.source("devmode 1\n" + excluded + retired) == self.base)
        self.check("automatic-disabled-malformed-tune", self.source("devmode 0\nmv_run_speed bad", False) == self.base)
        self.check("automatic-unmarked", self.source(excluded, False) == self.base)
        cloned = json.loads(Path("code/core/tuning-schema.json").read_bytes())
        cloned["aliases"] = {"run_alias": "mv_run_speed"}
        alias_path = self.out / "alias-schema.json"
        alias_path.write_text(json.dumps(cloned))
        alias = Schema(alias_path)
        self.check("declared-alias-normalizes", self.source("devmode 1\nrun_alias 7\nmv_run_speed 7.0", schema=alias)["mv_run_speed"] == "7")
        try:
            self.source("devmode 1\nrun_alias 7\nmv_run_speed 8", schema=alias)
        except TuningError:
            self.check("declared-alias-conflict", True)
        else:
            self.check("declared-alias-conflict", False)
        # A midpoint with more digits than binary64 can retain distinguishes
        # correct direct rounding from decimal -> double -> float conversion.
        midpoint = decimal.Decimal("1.000000059604644775390625")
        self.check("float32-ties-even", f32(midpoint) == 1.0)
        self.check("float32-no-double-round", f32(midpoint + decimal.Decimal("1e-27")) == struct.unpack("!f", bytes.fromhex("3f800001"))[0])
        source = self.out / "parse-replacement.cfg"
        source.write_text("devmode 1\nmv_run_speed 7\n")
        original = self.schema.validate
        def replace(key, token):
            candidate = source.with_suffix(".next")
            candidate.write_text("devmode 1\nmv_run_speed 8\n")
            candidate.replace(source)
            return original(key, token)
        with patch.object(self.schema, "validate", replace):
            try:
                self.schema.select(self.base, source, True)
            except TuningError:
                self.check("replace-during-parse-rejected", True)
            else:
                self.check("replace-during-parse-rejected", False)
        old = self.out / "atomic.json"
        old.write_bytes(b"prior")
        with patch("os.replace", side_effect=OSError("injected interrupted write")):
            try:
                atomic(old, b"partial")
            except OSError:
                pass
        self.check("interrupted-write-preserves-prior", old.read_bytes() == b"prior")
        module = build_module("build_game_control")
        with patch.dict(os.environ, {"WARN_CFLAGS": "-std=c23 -O0 -Wall -Wextra -Wshadow -Werror"}):
            linux_flags = module.artifact_spec("game-warning-linux.o")[1]
            windows_flags = module.artifact_spec("game-warning-windows.o")[1]
        self.check("warning-windows-cpu-floor", "-march=x86-64-v2" in windows_flags)
        self.check("warning-native-no-x86-floor", "-march=x86-64-v2" not in linux_flags)
        one, two = self.out / "publish-one", self.out / "publish-two"
        one.write_bytes(b"old-one")
        two.write_bytes(b"old-two")
        original_replace = os.replace
        def fail_second(source, target):
            if Path(target) == two:
                raise OSError("injected second publication failure")
            return original_replace(source, target)
        with patch("os.replace", fail_second):
            try:
                module.publish([(one, b"new-one", 0o644), (two, b"new-two", 0o644)])
            except OSError:
                pass
        self.check("publication-failure-rolls-back-group", one.read_bytes() == b"old-one" and two.read_bytes() == b"old-two")
        other_fs = next((Path(p) for p in ("/dev/shm", "/tmp") if Path(p).is_dir()
                         and os.access(p, os.W_OK) and Path(p).stat().st_dev != self.out.stat().st_dev), None)
        self.check("cross-filesystem-control-available", other_fs is not None)
        with tempfile.TemporaryDirectory(prefix="defaults-cross-fs-", dir=other_fs) as external:
            binary = Path(external) / "binary"
            binary.write_bytes(b"old-binary")
            module.publish([(one, b"cross-canonical", 0o644), (binary, b"cross-binary", 0o755)])
            self.check("cross-filesystem-publication", one.read_bytes() == b"cross-canonical" and binary.read_bytes() == b"cross-binary")

    def publication_cleanup_controls(self):
        module = build_module("build_game_cleanup")
        root = self.out / "publication-cleanup"
        root.mkdir()
        original_replace, original_unlink, original_rmdir = os.replace, os.unlink, Path.rmdir

        def pair(name):
            directory = root / name
            directory.mkdir()
            one, two = directory / "one", directory / "two"
            one.write_bytes(b"old-one")
            two.write_bytes(b"old-two")
            return directory, one, two

        directory, one, two = pair("known-paths")
        with patch("shutil.rmtree", side_effect=AssertionError("recursive cleanup used")):
            module.publish([(one, b"new-one", 0o644), (two, b"new-two", 0o644)])
        self.check("publication-explicit-owned-cleanup", not list(directory.glob(".tuning-publish-*")))

        directory, one, two = pair("unlink-denied")
        denied = []
        def deny_old(path, *args, **kwargs):
            if Path(path).name == "old":
                denied.append(str(path))
                raise PermissionError(errno.EACCES, "injected retained backup", str(path))
            return original_unlink(path, *args, **kwargs)
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr), patch("os.unlink", deny_old):
            module.publish([(one, b"new-one", 0o644)])
        self.check("publication-cleanup-denied-remains-success", one.read_bytes() == b"new-one")
        self.check("publication-cleanup-denied-reported-once", len(denied) == 1 and "outputs published" in stderr.getvalue() and str(directory) in stderr.getvalue())
        self.check("publication-cleanup-backup-retained", [p.read_bytes() for p in directory.glob(".tuning-publish-*/old")] == [b"old-one"])

        directory, one, two = pair("rmdir-notempty")
        def deny_rmdir(path):
            if path.name.startswith(".tuning-publish-"):
                raise OSError(errno.ENOTEMPTY, "injected delayed directory removal", str(path))
            return original_rmdir(path)
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr), patch.object(Path, "rmdir", deny_rmdir):
            module.publish([(one, b"new-one", 0o644)])
        self.check("publication-rmdir-error-remains-success", one.read_bytes() == b"new-one" and "outputs published" in stderr.getvalue())

        directory, one, two = pair("primary-error")
        primary = OSError("injected primary publication failure")
        def fail_second(source, target):
            if Path(target) == two:
                raise primary
            return original_replace(source, target)
        stderr = io.StringIO()
        caught = None
        with contextlib.redirect_stderr(stderr), patch("os.replace", fail_second), patch("os.unlink", deny_old):
            try:
                module.publish([(one, b"new-one", 0o644), (two, b"new-two", 0o644)])
            except OSError as error:
                caught = error
        self.check("publication-primary-error-preserved", caught is primary)
        self.check("publication-primary-error-restores-group", one.read_bytes() == b"old-one" and two.read_bytes() == b"old-two")
        self.check("publication-failed-cleanup-reported", "publication did not complete" in stderr.getvalue() and "outputs published" not in stderr.getvalue())

        directory, one, two = pair("rollback-error")
        three = directory / "three"
        three.write_bytes(b"old-three")
        primary = OSError("injected primary before failed rollback")
        def fail_rollback(source, target):
            if Path(target) == three:
                raise primary
            if Path(source).name == "old" and Path(target) == two:
                raise OSError("injected rollback failure")
            return original_replace(source, target)
        stderr = io.StringIO()
        caught = None
        with contextlib.redirect_stderr(stderr), patch("os.replace", fail_rollback):
            try:
                module.publish([(one, b"new-one", 0o644), (two, b"new-two", 0o644), (three, b"new-three", 0o644)])
            except OSError as error:
                caught = error
        self.check("publication-rollback-error-primary-preserved", caught is primary)
        self.check("publication-rollback-error-continues-restoration", one.read_bytes() == b"old-one" and two.read_bytes() == b"new-two" and three.read_bytes() == b"old-three")
        self.check("publication-rollback-error-recovery-retained", sorted(p.read_bytes() for p in directory.glob(".tuning-publish-*/old")) == [b"old-three", b"old-two"])
        self.check("publication-rollback-error-reported", "could not restore" in stderr.getvalue() and "recovery files retained" in stderr.getvalue())

    def tree(self):
        tree = self.out / "transaction"
        tree.mkdir()
        for directory in ("code", "tools"):
            shutil.copytree(directory, tree / directory, ignore=shutil.ignore_patterns("__pycache__"))
        shutil.copy2("Makefile", tree / "Makefile")
        (tree / "build").mkdir()
        shim = tree / "compiler"
        shim.write_text(SHIM)
        shim.chmod(0o755)
        env = os.environ.copy()
        env.pop("TUNING_SOURCE", None)
        for key in ("LIN_CC", "X86_CC", "WIN_CC", "WIN_RES"):
            env[key] = str(shim)
        return tree, env

    def invoke(self, tree, env, *args, ok=True):
        command = [sys.executable, "tools/build-game.py", *args]
        result = subprocess.run(command, cwd=tree, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        index = len(list(self.out.glob("build-*.log")))
        (self.out / f"build-{index:03}.log").write_bytes(result.stdout)
        self.check("transaction-exit-" + str(index), (result.returncode == 0) == ok)
        return result

    @staticmethod
    def state(tree):
        paths = [tree / "code/core/tuning-defaults.json"] + [p for p in (tree / "build").glob("game*") if p.is_file()]
        return {str(p.relative_to(tree)): (digest(p.read_bytes()), p.stat().st_mtime_ns) for p in paths}

    def barrier(self, path, process):
        # This is a test handshake deadline, not a game/compilation timeout.
        deadline = time.monotonic() + 10
        while not path.exists() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(.01)
        self.check("compiler-barrier-reached", path.exists())

    def transaction_controls(self):
        tree, env = self.tree()
        all_targets = ["game", "game-x86_64", "game.exe", "game-asan", "game-warning-linux.o", "game-warning-windows.o"]
        self.invoke(tree, env, *all_targets)
        initial = self.state(tree)
        module = build_module("build_game_early_pin")
        schema_file = tree / "code/core/tuning-schema.json"
        original_schema = schema_file.read_bytes()
        original_init = module.Schema.__init__
        def change_schema(instance, path):
            changed = json.loads(original_schema)
            changed["groups"]["mv"][0]["label"] += " CHANGED"
            schema_file.write_text(json.dumps(changed))
            original_init(instance, path)
        previous_cwd = Path.cwd()
        try:
            os.chdir(tree)
            with patch.dict(os.environ, env, clear=True), patch.object(module.Schema, "__init__", change_schema), (self.out / "early-schema-pin.log").open("w") as log, contextlib.redirect_stdout(log):
                try:
                    module.transaction(argparse.Namespace(source=None, targets=["game"], force=False))
                except module.TuningError as error:
                    self.check("schema-replacement-during-validation", "build source changed" in str(error))
                else:
                    self.check("schema-replacement-during-validation", False)
        finally:
            os.chdir(previous_cwd)
            schema_file.write_bytes(original_schema)
        self.check("early-schema-change-no-publication", self.state(tree) == initial)
        self.invoke(tree, env, *all_targets)
        self.check("incremental-preserves-all-mtimes", self.state(tree) == initial)
        self.invoke(tree, env, "--source", "missing.cfg", "game", ok=False)
        self.check("missing-explicit-preserves-all", self.state(tree) == initial)
        source = tree / "build/config.cfg"
        source.write_text("devmode 1\nmv_run_speed 7.123456789\npad_as_friction 0,5\nplayer_name secret\n")
        original = source.read_bytes()
        self.invoke(tree, env, *all_targets)
        self.check("promotion-input-unchanged", source.read_bytes() == original)
        record = json.loads((tree / "build/tuning/last-selection.json").read_bytes())
        tune = record["tuning_sha256"]
        self.check("all-artifacts-one-snapshot", all((tree / "build" / t).read_text().strip() == tune for t in all_targets))
        canonical = (tree / "code/core/tuning-defaults.json").read_text()
        self.check("excluded-not-in-canonical", "secret" not in canonical and "player_name" not in canonical and "devmode" not in canonical)
        promoted = self.state(tree)
        source.write_text("devmode 0\n")
        self.invoke(tree, env, *all_targets)
        self.check("disable-preserves-last-canonical", self.state(tree) == promoted)
        source.unlink()
        self.invoke(tree, env, *all_targets)
        self.check("remove-preserves-last-canonical", self.state(tree) == promoted)
        for header in (tree / "build/tuning/snapshots").glob("*/tuning.h"):
            header.unlink()
        self.invoke(tree, env, *all_targets)
        self.check("missing-generated-restored-no-recompile", self.state(tree) == promoted and bool(list((tree / "build/tuning/snapshots").glob("*/tuning.h"))))
        source.write_text("devmode 1\nmv_run_speed 99\n")
        self.invoke(tree, env, *all_targets, ok=False)
        self.check("invalid-promotion-no-partial-publish", self.state(tree) == promoted)
        source.chmod(0)
        self.invoke(tree, env, "game", ok=False)
        source.chmod(0o600)
        self.check("unreadable-source-preserves", self.state(tree) == promoted)
        source.write_text("devmode 1\nmv_run_speed 7.123456789\npad_as_friction 0.5\n")
        source.chmod(0o444)
        readonly_input = source.read_bytes()
        self.invoke(tree, env, "game")
        self.check("readonly-input-success-unchanged", source.read_bytes() == readonly_input)
        source.chmod(0o600)
        source.write_text("devmode 1\nmv_run_speed 8\n")
        canonical_path = tree / "code/core/tuning-defaults.json"
        canonical_path.chmod(0o444)
        self.invoke(tree, env, "game", ok=False)
        canonical_path.chmod(0o644)
        self.check("readonly-canonical-preserves", self.state(tree) == promoted)
        (tree / "control.json").write_text('{"fail":true}')
        self.invoke(tree, env, *all_targets, ok=False)
        self.check("compiler-failure-no-publication", self.state(tree) == promoted)
        (tree / "control.json").unlink()
        original_code = (tree / "code/game.c").read_text()
        (tree / "control.json").write_text(json.dumps({"replace": "code/game.c", "text": original_code + "\n// replaced during compile\n"}))
        self.invoke(tree, env, "game", ok=False)
        self.check("code-replacement-no-publication", self.state(tree) == promoted)
        (tree / "code/game.c").write_text(original_code)
        (tree / "control.json").unlink()
        # Source replacement during compile belongs to the NEXT invocation.
        (tree / "control.json").write_text(json.dumps({"replace": "build/config.cfg", "text": "devmode 1\nmv_run_speed 9\n"}))
        self.invoke(tree, env, "game", "game-x86_64")
        selected = json.loads(canonical_path.read_bytes())["values"]
        self.check("compile-replacement-pins-selected-snapshot", selected["mv_run_speed"] == "8" and (tree / "build/game").read_bytes() == (tree / "build/game-x86_64").read_bytes())
        (tree / "control.json").unlink()
        self.invoke(tree, env, "game")
        self.check("next-build-observes-replacement", json.loads(canonical_path.read_bytes())["values"]["mv_run_speed"] == "9")
        source.unlink()
        old = self.state(tree)
        future = time.time() + 864000
        os.utime(tree / "code/game.c", (future, future))
        self.invoke(tree, env, "game")
        self.check("future-mtime-content-equal-no-rebuild", self.state(tree) == old)
        with (tree / "code/game.c").open("a") as output:
            output.write("\n// fingerprint change\n")
        os.utime(tree / "code/game.c", (future, future))
        self.invoke(tree, env, "game")
        manifest = tree / "build/tuning/artifacts/game.json"
        old_fingerprint = json.loads(manifest.read_bytes())["fingerprint"]
        changed_env = env | {"BUILD_COMMIT": "new-stamp"}
        self.invoke(tree, changed_env, "game")
        self.check("stamp-fingerprint-rebuild", json.loads(manifest.read_bytes())["fingerprint"] != old_fingerprint)
        old_fingerprint = json.loads(manifest.read_bytes())["fingerprint"]
        self.invoke(tree, changed_env | {"LIN_CFLAGS": "-std=c23 -O1"}, "game")
        self.check("options-fingerprint-rebuild", json.loads(manifest.read_bytes())["fingerprint"] != old_fingerprint)
        (tree / "build/game").write_text("corrupt")
        self.invoke(tree, env, "game")
        self.check("corrupt-binary-rebuilt", (tree / "build/game").read_text() != "corrupt")
        (tree / "build/game").chmod(0o444)
        before = self.state(tree)
        self.invoke(tree, env, "--force", "game", ok=False)
        self.check("readonly-output-preserves", self.state(tree) == before)
        (tree / "build/game").chmod(0o755)
        manifest.unlink()
        self.invoke(tree, env, "game")
        self.check("missing-manifest-rebuilt", manifest.exists())
        fingerprint = json.loads(manifest.read_bytes())["fingerprint"]
        with (tree / "compiler").open("a") as output:
            output.write("\n# distinct compiler identity\n")
        self.invoke(tree, env, "game")
        self.check("compiler-identity-rebuilt", json.loads(manifest.read_bytes())["fingerprint"] != fingerprint)
        # The real Make graph must call the transaction once for the full goal
        # set, including -j, full rebuild, warning and sanitizer artifacts.
        overrides = [key + "=" + env[key] for key in ("LIN_CC", "X86_CC", "WIN_CC", "WIN_RES")]
        for goals in (["rebuild"], ["-j", "rebuild", "all", "build/game-asan", "warning-gate"],
                      ["-B", "build/game"], ["build/game.res.o"]):
            discarded = tree / "build/nested/.old-evidence"
            discarded.parent.mkdir(exist_ok=True)
            discarded.write_text("discard on rebuild")
            source.write_text("devmode 1\nmv_run_speed 8\n")
            prior_canonical = canonical_path.read_bytes()
            result = subprocess.run(["make", *goals, *overrides], cwd=tree, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            (self.out / ("make-" + "-".join(goals).replace("/", "_") + ".log")).write_bytes(result.stdout)
            self.check("make-goals-" + "-".join(goals), result.returncode == 0)
            if "rebuild" in goals:
                self.check("rebuild-removes-entire-tree-" + "-".join(goals), not discarded.exists() and not source.exists())
                self.check("rebuild-retains-canonical-" + "-".join(goals), canonical_path.read_bytes() == prior_canonical)
                last = json.loads((tree / "build/tuning/last-selection.json").read_bytes())
                self.check("rebuild-compiles-every-request-" + "-".join(goals), set(last["rebuilt"]) == set(last["artifacts"]))
                if goals == ["rebuild"]:
                    self.check("rebuild-three-shipping-targets", set(last["artifacts"]) == {"game", "game-x86_64", "game.exe"})
            if "all" in goals:
                last = json.loads((tree / "build/tuning/last-selection.json").read_bytes())
                self.check("make-parallel-one-six-artifact-snapshot", set(last["artifacts"]) == set(all_targets))
        for retired in ("clean", "deploy"):
            result = subprocess.run(["make", "-n", retired], cwd=tree, env=env, capture_output=True)
            self.check("removed-target-" + retired, result.returncode != 0)
        source.unlink(missing_ok=True)
        before = self.state(tree)
        self.invoke(tree, env, "--rebuild", ok=False)
        self.check("rebuild-without-targets-preserves-tree", self.state(tree) == before)
        # Populate an old timestamp-based pyc, then change source bytes while
        # preserving both length and mtime. The new stricter limit MUST execute.
        tuning_source = tree / "tools/tuning.py"
        original = tuning_source.read_bytes()
        original_stat = tuning_source.stat()
        py_compile.compile(str(tuning_source), doraise=True)
        tuning_source.write_bytes(original.replace(b"1048576", b"1048575"))
        os.utime(tuning_source, ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns))
        source.write_bytes(b"devmode 1\n#" + b"x" * (1048576 - 11))
        before = self.state(tree)
        self.invoke(tree, env, "game", ok=False)
        self.check("same-size-mtime-generator-bypasses-stale-pyc", self.state(tree) == before)
        source.unlink()
        tuning_source.write_bytes(original)
        os.utime(tuning_source, ns=(original_stat.st_atime_ns, original_stat.st_mtime_ns))
        self.concurrent_controls(tree, env)
        self.rebuild_controls(tree, env)
        self.trailer_controls(tree, env, overrides)

    def concurrent_controls(self, tree, env):
        (tree / "one.cfg").write_text("devmode 1\nmv_run_speed 10\n")
        (tree / "two.cfg").write_text("devmode 1\nmv_run_speed 11\n")
        (tree / "control.json").write_text('{"barrier":"compile"}')
        command = [sys.executable, "tools/build-game.py", "--source", "one.cfg", "game", "game-x86_64", "game.exe"]
        with (self.out / "concurrent-one.log").open("wb") as first_log, (self.out / "concurrent-two.log").open("wb") as second_log:
            first = subprocess.Popen(command, cwd=tree, env=env, stdout=first_log, stderr=subprocess.STDOUT)
            self.barrier(tree / "compile.ready", first)
            second = subprocess.Popen(command[:3] + ["two.cfg"] + command[4:], cwd=tree, env=env, stdout=second_log, stderr=subprocess.STDOUT)
            (tree / "compile.go").write_text("go")
            self.check("concurrent-both-success", first.wait() == 0 and second.wait() == 0)
        record = json.loads((tree / "build/tuning/last-selection.json").read_bytes())
        self.check("concurrent-last-complete-snapshot", json.loads((tree / "code/core/tuning-defaults.json").read_bytes())["values"]["mv_run_speed"] == "11" and all((tree / "build" / name).read_text().strip() == record["tuning_sha256"] for name in ("game", "game-x86_64", "game.exe")))
        (tree / "compile.ready").unlink()
        (tree / "compile.go").unlink()
        prior = self.state(tree)
        with (self.out / "interrupted-compile.log").open("wb") as log:
            interrupted = subprocess.Popen(command, cwd=tree, env=env, stdout=log, stderr=subprocess.STDOUT)
            self.barrier(tree / "compile.ready", interrupted)
            interrupted.send_signal(signal.SIGTERM)
            (tree / "compile.go").write_text("go")
            self.check("interrupted-build-fails", interrupted.wait() != 0)
        self.check("interrupted-build-preserves-outputs", self.state(tree) == prior)

    def rebuild_cleanup_controls(self):
        module = build_module("build_game_remove")
        root = self.out / "rebuild-cleanup"
        (root / "build/nested").mkdir(parents=True)
        marker = root / "build/nested/.keep"
        marker.write_text("discard")
        previous = Path.cwd()
        original = shutil.rmtree
        calls = []
        def transient(path):
            calls.append(path)
            if len(calls) == 1:
                raise PermissionError("injected host indexer handle")
            return original(path)
        try:
            os.chdir(root)
            with patch("shutil.rmtree", side_effect=transient), patch("time.sleep"):
                module.remove_build()
            self.check("rebuild-transient-denial-retried", len(calls) == 2 and not Path("build").exists())
            Path("build").mkdir()
            with patch("shutil.rmtree", side_effect=PermissionError("persistent denial")) as denied, patch("time.sleep"):
                try:
                    module.remove_build()
                except PermissionError:
                    self.check("rebuild-persistent-denial-fails", denied.call_count == 20 and Path("build").exists())
                else:
                    self.check("rebuild-persistent-denial-fails", False)
        finally:
            os.chdir(previous)

    def rebuild_controls(self, tree, env):
        sentinel = tree / "build/nested/.sentinel"
        sentinel.parent.mkdir(exist_ok=True)
        sentinel.write_text("old evidence")
        external = tree / "external"
        external.mkdir()
        (external / "keep").write_text("outside build")
        (tree / "build/external-link").symlink_to(external, target_is_directory=True)
        (tree / "control.json").write_text('{"barrier":"rebuild-compile"}')
        command = [sys.executable, "tools/build-game.py"]
        with (self.out / "rebuild-active.log").open("wb") as first_log, (self.out / "rebuild-waiter.log").open("wb") as second_log:
            first = subprocess.Popen(command + ["--force", "game"], cwd=tree, env=env, stdout=first_log, stderr=subprocess.STDOUT)
            self.barrier(tree / "rebuild-compile.ready", first)
            lock = (tree / ".build.lock").stat().st_ino
            second = subprocess.Popen(command + ["--rebuild", "game", "game.exe", "game-x86_64"], cwd=tree, env=env, stdout=second_log, stderr=subprocess.STDOUT)
            try:
                time.sleep(.2)
                self.check("rebuild-waits-for-active-compiler", second.poll() is None and sentinel.exists())
            finally:
                (tree / "rebuild-compile.go").write_text("go")
                first_status, second_status = first.wait(), second.wait()
            self.check("rebuild-after-active-compile-success", first_status == second_status == 0)
        self.check("rebuild-lock-inode-survives", (tree / ".build.lock").stat().st_ino == lock)
        self.check("rebuild-discards-nested-hidden-files", not sentinel.exists())
        self.check("rebuild-does-not-follow-symlink", (external / "keep").read_text() == "outside build")
        (tree / "control.json").unlink()

    def trailer_controls(self, tree, env, overrides):
        media = tree / "media"
        media.mkdir()
        (media / "media.py").write_text(
            "import json,pathlib,sys\n"
            "pathlib.Path('media-invocation.json').write_text(json.dumps(sys.argv[1:]))\n"
            "sys.exit(23 if pathlib.Path('media-fail').exists() else 0)\n")
        command = ["make", "trailer", *overrides]
        def run(label):
            result = subprocess.run(command, cwd=tree, env=env, capture_output=True, text=True)
            (self.out / (label + ".log")).write_text(result.stdout + result.stderr)
            self.check(label + "-elapsed", "trailer: elapsed " in result.stdout)
            return result
        result = run("trailer-success")
        self.check("trailer-success-status", result.returncode == 0 and "OK" in result.stdout)
        self.check("trailer-fresh-all-outputs", json.loads((tree / "media-invocation.json").read_text()) == ["all", "--fresh"])
        last = json.loads((tree / "build/tuning/last-selection.json").read_text())
        self.check("trailer-forces-native-compile", last["rebuilt"] == ["game"])
        (tree / "media-fail").touch()
        result = run("trailer-media-failure")
        self.check("trailer-media-failure-propagates", result.returncode != 0 and "FAILED" in result.stdout)
        (tree / "media-invocation.json").unlink()
        (tree / "control.json").write_text('{"fail":true}')
        result = run("trailer-compile-failure")
        self.check("trailer-compile-failure-stops-media", result.returncode != 0 and not (tree / "media-invocation.json").exists())
        (tree / "control.json").unlink()
        dry = subprocess.run(["make", "-n", "trailer", *overrides], cwd=tree, env=env, capture_output=True)
        self.check("trailer-dry-run-does-not-render", dry.returncode == 0 and not (tree / "media-invocation.json").exists())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--outdir")
    args = parser.parse_args()
    Path("build").mkdir(exist_ok=True)
    out = Path(args.outdir) if args.outdir else Path(tempfile.mkdtemp(prefix="defaults-gate-", dir="build"))
    out.mkdir(parents=True, exist_ok=True)
    gate = Gate(out.resolve())
    try:
        gate.parser_controls()
        gate.publication_cleanup_controls()
        gate.rebuild_cleanup_controls()
        gate.transaction_controls()
    finally:
        (out / "results.json").write_text(json.dumps(gate.rows, indent=2) + "\n")
    print("defaults-gate: " + str(len(gate.rows)) + " checks OK; " + str(out))


if __name__ == "__main__":
    main()
