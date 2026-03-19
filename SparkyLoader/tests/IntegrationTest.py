#!/usr/bin/env python3
"""
IntegrationTest.py — Sparky end-to-end integration test suite.

Three independent test phases:

  Phase 1 · Injection Bypass
    Starts AntiCheatSim.exe, injects DummyDll.dll via InjectDirect.exe,
    then waits for the injected DLL's success signal.  Collects any
    DETECT: lines from the simulator and fails the test if any are found.

  Phase 2 · Server Protocol Smoke Test
    Starts SparkyServer.exe with a throwaway SQLite test database and sends
    a raw Hello packet over a plain TCP socket.  A well-formed AuthFail
    response (server running, access correctly denied) counts as a PASS.

  Phase 3 · Full End-to-End (documented, manual setup required)
    Described in the output when the other phases complete; requires a
    running PostgreSQL instance and a seeded license key.

Usage:
    python IntegrationTest.py [--bin-dir <path>]  [--phase 1|2|all]

    --bin-dir   Directory containing the compiled binaries.
                Default: <this_script>/../bin/  (CMake runtime output dir)
    --phase     Run only the specified phase (default: all)

Requirements (Windows only):
    Python 3.10+   — uses structural pattern matching in output parsing
    No third-party packages — only stdlib + ctypes (both ship with Python)

Exit code:
    0  all selected phases passed
    1  at least one phase failed
"""

import argparse
import ctypes
import os
import queue
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

# ---------------------------------------------------------------------------
# Colour helpers (no dependencies — works on Windows 10+ with ANSI support)
# ---------------------------------------------------------------------------
_RESET = "\033[0m"
_GREEN = "\033[92m"
_RED   = "\033[91m"
_CYAN  = "\033[96m"
_BOLD  = "\033[1m"

def _ok(msg:  str) -> str: return f"{_GREEN}[PASS]{_RESET} {msg}"
def _fail(msg: str) -> str: return f"{_RED}[FAIL]{_RESET} {msg}"
def _info(msg: str) -> str: return f"{_CYAN}[INFO]{_RESET} {msg}"

def _enable_ansi() -> None:
    """Enable ANSI escape sequences on Windows 10+ console."""
    if sys.platform == "win32":
        try:
            kernel32 = ctypes.windll.kernel32
            kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
        except Exception:
            pass

# ---------------------------------------------------------------------------
# Windows named-event helpers via ctypes (avoids pywin32 dependency)
# ---------------------------------------------------------------------------
def _wait_for_event(name: str, timeout_ms: int) -> bool:
    """
    Open a Windows named event and wait for it to be signalled.
    Returns True if signalled within timeout_ms, False on timeout / error.
    """
    if sys.platform != "win32":
        return False  # non-Windows: nothing to wait on

    kernel32 = ctypes.windll.kernel32
    SYNCHRONIZE       = 0x00100000
    WAIT_OBJECT_0     = 0x00000000
    WAIT_TIMEOUT_CODE = 0x00000102

    # Try Global\ namespace first (AntiCheatSim creates it with NULL DACL).
    handle = kernel32.OpenEventW(SYNCHRONIZE, False, f"Global\\{name}")
    if not handle:
        handle = kernel32.OpenEventW(SYNCHRONIZE, False, name)
    if not handle:
        return False

    result = kernel32.WaitForSingleObject(handle, timeout_ms)
    kernel32.CloseHandle(handle)
    return result == WAIT_OBJECT_0


# ---------------------------------------------------------------------------
# Background thread: drain a subprocess stdout into a queue, line-by-line.
# ---------------------------------------------------------------------------
def _drain_stdout(proc: subprocess.Popen, q: "queue.Queue[str]") -> None:
    try:
        for raw in proc.stdout:
            q.put(raw.decode("utf-8", errors="replace").rstrip())
    except Exception:
        pass
    q.put(None)  # sentinel


# ---------------------------------------------------------------------------
# Phase 1 — Injection Bypass Test
# ---------------------------------------------------------------------------
def phase1_injection_bypass(bin_dir: Path) -> bool:
    """
    1. Start AntiCheatSim.exe — it creates the success event and starts scanning.
    2. Parse its "READY:<pid>" line to get the target PID.
    3. Run InjectDirect.exe <pid> DummyDll.dll.
    4. Wait up to 10 s for the named event "SparkyDllSuccess".
    5. Wait 2 s for any late DETECT: lines.
    6. Terminate the simulator.
    7. PASS = event signalled + zero detections.
    """
    print(f"\n{_BOLD}=== Phase 1: Injection Bypass ==={_RESET}")

    sim_exe    = bin_dir / "AntiCheatSim.exe"
    inject_exe = bin_dir / "InjectDirect.exe"
    dll_path   = bin_dir / "DummyDll.dll"

    for p in (sim_exe, inject_exe, dll_path):
        if not p.exists():
            print(_fail(f"Required binary not found: {p}"))
            print(_info("Build target 'AntiCheatSim', 'InjectDirect', and 'DummyDll' first."))
            return False

    # Start the simulator.
    print(_info(f"Starting {sim_exe.name} …"))
    sim_proc = subprocess.Popen(
        [str(sim_exe)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == "win32" else 0,
    )

    line_q: queue.Queue[str] = queue.Queue()
    drain_thread = threading.Thread(target=_drain_stdout, args=(sim_proc, line_q), daemon=True)
    drain_thread.start()

    # Wait for READY:<pid>
    target_pid: int | None = None
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        try:
            line = line_q.get(timeout=0.2)
        except queue.Empty:
            continue
        if line is None:
            print(_fail("AntiCheatSim exited before sending READY line."))
            return False
        print(_info(f"  sim> {line}"))
        if line.startswith("READY:"):
            try:
                target_pid = int(line.split(":", 1)[1])
            except ValueError:
                pass
            break

    if target_pid is None:
        print(_fail("Timed out waiting for READY:<pid> from AntiCheatSim."))
        sim_proc.terminate()
        return False

    print(_info(f"  Target PID: {target_pid}"))

    # Inject.
    print(_info(f"Running InjectDirect.exe {target_pid} {dll_path.name} …"))
    inj = subprocess.run(
        [str(inject_exe), str(target_pid), str(dll_path)],
        capture_output=True,
        text=True,
    )
    for ln in inj.stdout.splitlines():
        print(_info(f"  inj> {ln}"))
    for ln in inj.stderr.splitlines():
        print(_info(f"  inj! {ln}"))

    if inj.returncode != 0:
        print(_fail(f"InjectDirect failed (exit {inj.returncode})."))
        sim_proc.terminate()
        return False

    # Wait for the DLL's success event.
    print(_info("Waiting for SparkyDllSuccess event (10 s) …"))
    dll_success = _wait_for_event("SparkyDllSuccess", 10_000)

    # Give the scanners 2 more seconds to catch anything late.
    time.sleep(2.0)

    # Collect all detection lines buffered so far.
    detections: list[str] = []
    while True:
        try:
            line = line_q.get_nowait()
        except queue.Empty:
            break
        if line is None:
            break
        print(_info(f"  sim> {line}"))
        if line.startswith("DETECT:"):
            detections.append(line)

    # Terminate the simulator.
    try:
        sim_proc.terminate()
        sim_proc.wait(timeout=5)
    except Exception:
        sim_proc.kill()

    # Verdict.
    if not dll_success:
        print(_fail("DummyDll did not signal success within 10 s."))
        return False

    if detections:
        print(_fail(f"{len(detections)} detection(s) raised by the simulator:"))
        for d in detections:
            parts = d.split(":", 2)
            watchdog = parts[1] if len(parts) > 1 else "?"
            detail   = parts[2] if len(parts) > 2 else d
            print(f"   {_RED}[{watchdog}]{_RESET} {detail}")
        return False

    print(_ok("Injection succeeded — DLL signalled success, zero detections raised."))
    return True


# ---------------------------------------------------------------------------
# Phase 2 — Server Protocol Smoke Test
# ---------------------------------------------------------------------------

# Protocol constants (must match Protocol.h)
PROTO_MAGIC   = 0x53504B59   # "SPKY"
PROTO_VERSION = 3
MSG_HELLO     = 0x01
MSG_AUTH_FAIL = 0x11
LISTEN_PORT   = 7777
CURRENT_BUILD = 0x0001_0000  # must match SparkyServer CURRENT_BUILD constant


def _build_hello() -> bytes:
    """
    Build a raw MsgHeader + HelloPayload with zeroed HWID, current BuildId,
    and zeroed LoaderHash.  The server will reject this (no matching license)
    and reply with AuthFail — which is the expected result for a smoke test.
    """
    # MsgHeader: magic(4) + version(1) + type(1) + length(2) + pad(4) = 12 bytes
    # HelloPayload: HwidHash(32) + BuildId(4) + LoaderHash(32) = 68 bytes
    payload = struct.pack("<32sI32s",
                          b"\x00" * 32,    # HwidHash (zeroed)
                          CURRENT_BUILD,   # BuildId
                          b"\x00" * 32)    # LoaderHash (zeroed)
    header = struct.pack("<IBBH4s",
                         PROTO_MAGIC,
                         PROTO_VERSION,
                         MSG_HELLO,
                         len(payload),
                         b"\x00\x00\x00\x00")  # Pad
    return header + payload


def _recv_exact(sock: socket.socket, n: int, timeout: float = 5.0) -> bytes | None:
    sock.settimeout(timeout)
    buf = b""
    deadline = time.monotonic() + timeout
    while len(buf) < n:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return None
        sock.settimeout(remaining)
        try:
            chunk = sock.recv(n - len(buf))
        except (socket.timeout, ConnectionError):
            return None
        if not chunk:
            return None
        buf += chunk
    return buf


def phase2_server_protocol(bin_dir: Path) -> bool:
    """
    1. Start SparkyServer.exe with SPARKY_DB_PATH pointing to a temp file.
    2. Connect a raw TCP socket.
    3. Send a Hello with a zeroed HWID (no matching license in the test DB).
    4. Expect AuthFail (0x11) response.
    5. PASS = server is up and correctly rejects the unknown client.
    """
    print(f"\n{_BOLD}=== Phase 2: Server Protocol Smoke Test ==={_RESET}")

    server_exe = bin_dir / "SparkyServer.exe"
    if not server_exe.exists():
        print(_fail(f"SparkyServer.exe not found at {server_exe}"))
        return False

    # Temporary DB file so the test is fully isolated.
    import tempfile
    tmp_db = tempfile.NamedTemporaryFile(suffix=".db", delete=False)
    tmp_db.close()

    env = os.environ.copy()
    # Point the server at an empty throw-away SQLite database.
    # SparkyServer reads SPARKY_DB_PATH when present (SQLite path).
    env["SPARKY_DB_PATH"] = tmp_db.name
    # Disable TLS in test mode — no cert/key next to test binary.
    # SparkyServer falls back to plaintext if cert/key are absent; we just
    # make sure no stale files are picked up.
    env.pop("SPARKY_TLS_CERT", None)
    env.pop("SPARKY_TLS_KEY",  None)

    print(_info(f"Starting {server_exe.name} (plain TCP, temp DB) …"))
    srv_proc = subprocess.Popen(
        [str(server_exe)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == "win32" else 0,
    )

    # Give the server 4 s to bind its port.
    connected = False
    sock: socket.socket | None = None
    for _ in range(20):
        time.sleep(0.2)
        try:
            sock = socket.create_connection(("127.0.0.1", LISTEN_PORT), timeout=1.0)
            connected = True
            break
        except (ConnectionRefusedError, TimeoutError, OSError):
            pass

    if not connected or sock is None:
        print(_fail(f"Could not connect to SparkyServer on port {LISTEN_PORT} within 4 s."))
        print(_info("Check that SPARKY_DB_PATH and port are correct, and no firewall blocks."))
        srv_proc.terminate()
        Path(tmp_db.name).unlink(missing_ok=True)
        return False

    print(_info(f"  Connected to 127.0.0.1:{LISTEN_PORT}"))

    # Send Hello.
    hello = _build_hello()
    try:
        sock.sendall(hello)
    except Exception as e:
        print(_fail(f"send() failed: {e}"))
        sock.close()
        srv_proc.terminate()
        Path(tmp_db.name).unlink(missing_ok=True)
        return False

    # Read the response header (12 bytes).
    hdr_raw = _recv_exact(sock, 12, timeout=6.0)
    sock.close()
    srv_proc.terminate()
    srv_proc.wait(timeout=5)
    Path(tmp_db.name).unlink(missing_ok=True)

    if hdr_raw is None or len(hdr_raw) < 12:
        print(_fail("Server did not respond within 6 s."))
        return False

    # Parse MsgHeader.
    magic, version, msg_type, length, _pad = struct.unpack("<IBBH4s", hdr_raw)
    print(_info(f"  Response: magic=0x{magic:08X} version={version} type=0x{msg_type:02X} length={length}"))

    if magic != PROTO_MAGIC:
        print(_fail(f"Bad magic in response: 0x{magic:08X} (expected 0x{PROTO_MAGIC:08X})"))
        return False

    if version != PROTO_VERSION:
        print(_fail(f"Protocol version mismatch: got {version}, expected {PROTO_VERSION}"))
        return False

    if msg_type == MSG_AUTH_FAIL:
        print(_ok("Server correctly rejected unknown HWID with AuthFail — protocol handshake verified."))
        return True
    else:
        # AuthOk would be surprising with a zeroed/unknown HWID.
        print(_fail(f"Unexpected message type 0x{msg_type:02X} (expected AuthFail 0x{MSG_AUTH_FAIL:02X})"))
        return False


# ---------------------------------------------------------------------------
# Phase 3 — Full E2E documentation banner
# ---------------------------------------------------------------------------
def phase3_full_e2e_info() -> None:
    print(f"\n{_BOLD}=== Phase 3: Full End-to-End (manual setup) ==={_RESET}")
    print(_info("This phase requires:"))
    print(_info("  1. A running PostgreSQL instance with the Sparky schema applied."))
    print(_info("     Set SPARKY_PG_CONNSTR or write a sparky.connstr file next to SparkyServer.exe."))
    print(_info("  2. A valid license key seeded in the licenses table:"))
    print(_info("       INSERT INTO licenses VALUES ('TEST-TEST-TEST-TEST', 4, unixepoch(), 0, '', 'ci');"))
    print(_info("  3. sparky.crt + sparky.key next to SparkyServer.exe for TLS,"))
    print(_info("     and the matching fingerprint pasted into CertPin.h."))
    print(_info("  4. SparkyCore.dll (the real payload) present next to SparkyServer.exe."))
    print(_info("  5. Run SparkyLoader.exe, enter the test license, choose AntiCheatSim.exe as target."))
    print(_info("  Expected outcome: DummyDll (or SparkyCore) loaded silently, zero DETECT: lines."))


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------
def main() -> int:
    _enable_ansi()

    parser = argparse.ArgumentParser(description="Sparky integration test suite")
    parser.add_argument(
        "--bin-dir",
        default=None,
        help="Directory containing compiled binaries (default: ../bin relative to this script)",
    )
    parser.add_argument(
        "--phase",
        default="all",
        choices=["1", "2", "all"],
        help="Which test phase(s) to run (default: all)",
    )
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    if args.bin_dir:
        bin_dir = Path(args.bin_dir).resolve()
    else:
        bin_dir = (script_dir / ".." / "bin").resolve()

    print(f"{_BOLD}Sparky Integration Tests{_RESET}")
    print(f"  Binary directory : {bin_dir}")
    print(f"  Platform         : {sys.platform}")

    if sys.platform != "win32":
        print(_fail("Integration tests require Windows — all binaries are Win32/x64."))
        return 1

    results: dict[str, bool] = {}

    if args.phase in ("1", "all"):
        results["Phase 1 · Injection Bypass"] = phase1_injection_bypass(bin_dir)

    if args.phase in ("2", "all"):
        results["Phase 2 · Server Protocol"] = phase2_server_protocol(bin_dir)

    if args.phase == "all":
        phase3_full_e2e_info()

    # Summary table.
    print(f"\n{_BOLD}{'─'*50}{_RESET}")
    all_pass = True
    for name, passed in results.items():
        status = _ok(name) if passed else _fail(name)
        print(f"  {status}")
        if not passed:
            all_pass = False
    print(f"{_BOLD}{'─'*50}{_RESET}")
    print(_ok("All phases passed.") if all_pass else _fail("One or more phases failed."))
    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
