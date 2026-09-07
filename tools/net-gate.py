#!/usr/bin/env python3
"""Native Linux UDP integration gate: two clients, events, rejoin and server loss.

Uses the shipping binary for both roles and keeps all logs/configs in a fresh
build/ directory (or under the supplied evidence directory). Timing statistics
are evidence, not scheduler-dependent pass/fail thresholds on shared CI hosts.
"""

import concurrent.futures
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time


def main():
    binary = pathlib.Path(sys.argv[1]).resolve()
    parent = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "build")
    parent.mkdir(parents=True, exist_ok=True)
    evidence = pathlib.Path(tempfile.mkdtemp(prefix="net-gate-", dir=parent)).resolve()
    server = None

    def client(name, port, ticks=600):
        with (evidence / f"{name}.log").open("w") as log:
            result = subprocess.run(
                [str(binary), "--seed", "1337", "--config", str(evidence / f"{name}.cfg"),
                 "--do", f"netclient 127.0.0.1 {port} {ticks} forward,fire"],
                stdout=log, stderr=subprocess.STDOUT, timeout=30, check=False)
        output = (evidence / f"{name}.log").read_text()
        rows = re.findall(r"^netclient CONNECTED (.+)$", output, re.MULTILINE)
        return result.returncode, rows

    def stop_server():
        if server is not None and server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        with (evidence / "server.log").open("w") as log:
            server = subprocess.Popen(
                [str(binary), "--server", "--seed", "1337", "--config",
                 str(evidence / "server.cfg"), "--port", str(port), "--lobbies", "2",
                 "--cap-public", "4", "--cap-bots", "1", "--fraglimit", "1000"],
                stdout=log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 10
            while "server port=" not in (evidence / "server.log").read_text():
                if server.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("server did not become ready")
                time.sleep(0.02)
            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                results = list(pool.map(lambda name: client(name, port), ("client-a", "client-b")))
                entities = set()
                for status, rows in results:
                    if status != 0 or len(rows) != 1:
                        raise RuntimeError("client failed to join and sustain snapshots")
                    fields = dict(item.split("=", 1) for item in rows[0].split() if "=" in item)
                    if int(fields["ev_applied"]) <= 0 or fields["reject"] != "0":
                        raise RuntimeError("authoritative events missing or session rejected")
                    entities.add(fields["ent"])
                    print("net-gate client " + rows[0])
                if len(entities) != 2:
                    raise RuntimeError("concurrent clients received the same entity")
                status, rows = client("rejoin", port, 120)
                if status != 0 or len(rows) != 1:
                    raise RuntimeError("leave/rejoin failed")
                dropped = pool.submit(client, "server-loss", port, 1200)
                time.sleep(1)
                stop_server()
                status, rows = dropped.result()
                if status == 0 or rows:
                    raise RuntimeError("server loss was reported as a successful connection")
        print("net-gate: OK (two clients, snapshots/events, rejoin, server loss)")
        return 0
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"net-gate: FAIL: {error}", file=sys.stderr)
        return 1
    finally:
        stop_server()
        print(f"net-gate: evidence {evidence}")


if __name__ == "__main__":
    if not 2 <= len(sys.argv) <= 3:
        sys.exit("usage: net-gate.py BINARY [EVIDENCE_DIRECTORY]")
    sys.exit(main())
