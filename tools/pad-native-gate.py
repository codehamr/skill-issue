#!/usr/bin/env python3
"""Compile and run the native evdev packet/ownership fixture without real devices."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
source = Path('tools/tuning.py')
namespace = {'__name__': 'pad_gate_tuning', '__file__': str(source)}
exec(compile(source.read_bytes(), str(source), 'exec'), namespace)
schema = namespace['Schema']('code/core/tuning-schema.json')
values = schema.canonical('code/core/tuning-defaults.json')
out = Path(tempfile.mkdtemp(prefix='pad-native-gate-', dir='build')).resolve()
(out / 'tuning.h').write_bytes(schema.header(values))
command = shlex.split(os.environ.get('LIN_CC', 'gcc')) + [
    '-std=c23', '-O1', '-Wall', '-Wextra', '-Wshadow', '-Werror', '-I' + str(out),
    'tools/pad-native-gate.c', '-o', str(out / 'gate'), '-lEGL', '-lGL', '-lX11', '-lm']
with (out / 'compile.log').open('w') as log:
    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
if result.returncode:
    print((out / 'compile.log').read_text())
    raise SystemExit(result.returncode)
result = subprocess.run([str(out / 'gate')], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(out / 'result.log').write_text(result.stdout)
print(result.stdout, end='')
print('pad-native-gate: evidence', out)
raise SystemExit(result.returncode)
