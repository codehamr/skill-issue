#!/usr/bin/env python3
"""Shared declarative tuning schema, strict promotion and derived C tables.

Runtime parsing remains tolerant. Retired keys are consumed without importing
their obsolete semantics; aliases, when declared, name one canonical key.
"""
import decimal
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import tempfile
from fractions import Fraction


class TuningError(Exception):
    pass


def packed(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def readable(path):
    path = Path(path)
    mode = path.stat().st_mode
    if not stat.S_ISREG(mode) or not mode & 0o444:
        raise TuningError("input is not a readable regular file")
    with path.open("rb") as source:
        before = os.fstat(source.fileno())
        data = source.read(1048577)
        after = os.fstat(source.fileno())
    identity = (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
    if identity != (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
        raise TuningError("input changed while reading")
    if len(data) > 1048576:
        raise TuningError("input exceeds 1 MiB")
    return data, identity


def unchanged(path, data, identity):
    current, current_identity = readable(path)
    if identity != current_identity or data != current:
        raise TuningError("input replaced or modified during validation")


def writable(path):
    path = Path(path)
    if path.exists() and not path.stat().st_mode & 0o222:
        raise TuningError("destination is read only")
    if not path.parent.stat().st_mode & 0o222:
        raise TuningError("destination directory is read only")


def atomic(path, data, mode=0o644):
    path = Path(path)
    if path.exists() and path.read_bytes() == data:
        return False
    writable(path)
    fd, name = tempfile.mkstemp(prefix="." + path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as out:
            os.fchmod(out.fileno(), mode)
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        os.replace(name, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(name):
            os.unlink(name)
    return True


NUMBER = re.compile(r"[+-]?(?:[0-9]+(?:[.,][0-9]*)?|[.,][0-9]+)(?:[eE][+-]?[0-9]+)?\Z")


def number(token):
    if not isinstance(token, str) or len(token) > 128 or not NUMBER.fullmatch(token):
        raise TuningError("malformed finite decimal")
    try:
        value = decimal.Decimal(token.replace(",", "."))
    except decimal.InvalidOperation as exc:
        raise TuningError("malformed decimal") from exc
    if not value.is_finite():
        raise TuningError("nonfinite decimal")
    return value


def f32(value):
    """Round the exact decimal directly to nearest-even binary32."""
    if not value or value.adjusted() < -200:
        return 0.0
    approximate = struct.unpack("!I", struct.pack("!f", float(value)))[0]
    exact = Fraction(value)
    candidates = range(max(0, approximate - 1), min(0x7f7fffff, approximate + 1) + 1)
    bits = min(candidates, key=lambda b: (
        abs(Fraction(struct.unpack("!f", struct.pack("!I", b))[0]) - exact), b & 1))
    return struct.unpack("!f", struct.pack("!I", bits))[0]


def spelling(value):
    return format(value, ".9g")


class Schema:
    def __init__(self, path):
        self.data = json.loads(readable(path)[0])
        self.hash = digest(packed(self.data))
        self.version = self.data["version"]
        self.rows = {}
        for group, entries in self.data["groups"].items():
            for row in entries:
                prefixes = [w["prefix"] + "_" for w in self.data["weapons"]] if group == "wp" else [""]
                for prefix in prefixes:
                    key = prefix + row["key"]
                    if key in self.rows:
                        raise TuningError("duplicate schema key")
                    self.rows[key] = row
        self.aliases = self.data["aliases"]
        if any(k in self.rows or v not in self.rows for k, v in self.aliases.items()):
            raise TuningError("invalid schema alias")
        self.retired = self.data["retired"] + [w["prefix"] + "_" + k
            for w in self.data["weapons"] for k in self.data["retired_weapon"]]
        if len(self.rows) != 24 or set(self.retired) & (self.rows.keys() | self.aliases.keys()):
            raise TuningError("schema must name exactly 24 distinct eligible values")

    def validate(self, key, token):
        value = number(token)
        row = self.rows[key]
        if value < number(row["lo"]) or value > number(row["hi"]):
            raise TuningError("eligible value out of range: " + key)
        return value

    def canonical(self, path):
        document = json.loads(readable(path)[0])
        if document["schema_version"] != self.version or set(document["values"]) != set(self.rows):
            raise TuningError("canonical keys or schema version disagree")
        return {k: spelling(f32(self.validate(k, v))) for k, v in document["values"].items()}

    def select(self, canonical, source, explicit):
        try:
            data, identity = readable(source)
        except FileNotFoundError:
            if os.path.lexists(source):
                raise TuningError("present tuning source cannot be read") from None
            if explicit:
                raise TuningError("explicit tuning source is missing") from None
            return canonical.copy(), {"selection": "absent"}, None
        # Decode only relevant ASCII keys/values. Excluded user text, including
        # arbitrary byte strings, cannot change the selected gameplay defaults.
        lines = []
        for raw in data.splitlines():
            line = raw.decode("utf-8", errors="surrogateescape").strip()
            if not line or line.startswith("#"):
                continue
            words = line.split(None, 1)
            key = words[0]
            if key != "devmode" and key not in self.rows and key not in self.aliases:
                continue
            value = words[1] if len(words) == 2 else ""
            value = re.sub(r"\s+#.*$", "", value).strip()
            lines.append((key, value))
        markers = [value for key, value in lines if key == "devmode"]
        if any(value not in ("0", "1") for value in markers) or len(set(markers)) > 1:
            raise TuningError("malformed or conflicting devmode marker")
        eligible = bool(markers) and markers[0] == "1"
        if explicit and not eligible:
            raise TuningError("explicit tuning source requires devmode 1")
        values, seen = canonical.copy(), {}
        if eligible:
            for key, token in lines:
                if key == "devmode":
                    continue
                key = self.aliases.get(key, key)
                value = self.validate(key, token)
                if key in seen and seen[key] != value:
                    raise TuningError("conflicting eligible values: " + key)
                seen[key] = value
                values[key] = spelling(f32(value))
        unchanged(source, data, identity)
        return values, {"selection": "eligible" if eligible else "disabled",
                        "source_sha256": digest(data), "selected": {k: values[k] for k in sorted(seen)}}, (data, identity)

    def document(self, values):
        return (json.dumps({"schema_version": self.version, "values": values},
                           indent=2, sort_keys=True) + "\n").encode()

    def tuning_hash(self, values):
        return digest(packed({"schema": self.hash, "values": {
            k: struct.pack("!f", float(v)).hex() for k, v in values.items()}}))

    def header(self, values):
        def literal(text):
            return float(f32(number(text))).hex() + "f"

        def macro(name, lines):
            return "#define " + name + " \\\n" + " \\\n".join("  " + line for line in lines) + "\n"

        out = "// Generated from the canonical tuning schema; never edit.\n#pragma once\n"
        out += '#define TUNING_SCHEMA_SHA "' + self.hash + '"\n'
        out += '#define TUNING_SHA "' + self.tuning_hash(values) + '"\n'
        for group, entries in self.data["groups"].items():
            lines = []
            for row in entries:
                default = "0" if group == "wp" else values[row["key"]]
                numbers = ", ".join(literal(v) for v in (default, row["lo"], row["hi"], row["step"]))
                lines.append(f'[{row["enum"]}] = {{{json.dumps(row["key"])}, {json.dumps(row["label"])}, {numbers}}},')
            out += macro("TUNING_" + group.upper() + "_ROWS", lines)
            out += f"#define TUNING_{group.upper()}_COUNT {len(entries)}\n"
        lines = []
        for weapon in self.data["weapons"]:
            prefix = weapon["prefix"]
            out += f'#define TUNING_{weapon["enum"]}_PREFIX "{prefix}"\n'
            defaults = ", ".join(f'[{r["enum"]}] = {literal(values[prefix + "_" + r["key"]])}'
                                 for r in self.data["groups"]["wp"])
            lines.append(f'[{weapon["enum"]}] = {{{defaults}}},')
        out += macro("TUNING_WP_DEFAULT_ROWS", lines)
        out += macro("TUNING_RETIRED_KEYS", [json.dumps(k) + "," for k in self.retired])
        out += macro("TUNING_ALIAS_ROWS", [f'{{{json.dumps(k)}, {json.dumps(v)}}},'
            for k, v in self.aliases.items()] + ["{NULL, NULL}"])
        return out.encode()
