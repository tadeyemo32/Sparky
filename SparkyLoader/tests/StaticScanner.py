#!/usr/bin/env python3
"""
StaticScanner.py — "Flag Fiction" binary analyser for SparkyLoader.exe.

Scans a compiled Windows PE binary for three classes of indicators that
anti-cheat telemetry and AV heuristics routinely flag:

  1. Plaintext strings  — sensitive function / DLL names visible in the
                          binary's string tables (LoadLibraryA, ntdll.dll …).
                          These appear when the compiler embeds string literals
                          that your loader uses at runtime.  Presence means an
                          AC can grep your binary and know exactly what it does.

  2. Import table (IAT) — Win32 API names listed in the binary's import
                          directory.  High-signal functions (CreateRemoteThread,
                          WriteProcessMemory …) in the IAT are instant "ban on
                          sight" indicators for most commercial ACs.

  3. Section entropy     — Shannon entropy of each PE section.  A section
                          entropy below ~3.5 bits/byte in what should be an
                          encrypted payload means the "encryption" is too
                          predictable.  Entropy above ~7.9 bits/byte in a
                          code section suggests a packer (may trigger sandbox
                          detonation rather than a direct ban, but still bad).

Usage:
    python StaticScanner.py [<binary>]  [--fail-on-warn]

    <binary>         Path to the PE to scan.
                     Default: ../bin/SparkyLoader.exe
    --fail-on-warn   Exit 1 even when only warnings (not hard failures) fire.

Exit codes:
    0   clean (no hard failures; warnings may be present unless --fail-on-warn)
    1   hard failure (e.g. CreateRemoteThread in IAT) OR --fail-on-warn + warnings
    2   usage / file-not-found error

Requirements:
    Python 3.10+, no third-party packages.
"""

import argparse
import math
import struct
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# ANSI colour helpers
# ---------------------------------------------------------------------------
_RESET = "\033[0m"
_GREEN = "\033[92m"
_RED   = "\033[91m"
_YELLOW= "\033[93m"
_CYAN  = "\033[96m"
_BOLD  = "\033[1m"

def _ok(msg:   str) -> str: return f"{_GREEN}  [CLEAN]{_RESET}  {msg}"
def _warn(msg: str) -> str: return f"{_YELLOW}  [WARN] {_RESET}  {msg}"
def _bad(msg:  str) -> str: return f"{_RED}  [FLAG] {_RESET}  {msg}"
def _info(msg: str) -> str: return f"{_CYAN}  [INFO] {_RESET}  {msg}"

def _enable_ansi() -> None:
    if sys.platform == "win32":
        try:
            import ctypes
            ctypes.windll.kernel32.SetConsoleMode(
                ctypes.windll.kernel32.GetStdHandle(-11), 7)
        except Exception:
            pass

# ---------------------------------------------------------------------------
# "Banned" string list — presence in the binary is a hard failure.
# These are strings that AC telemetry scans binaries for.
# ---------------------------------------------------------------------------
HARD_FAIL_STRINGS: list[tuple[str, str]] = [
    # (pattern,                    reason)
    ("CreateRemoteThread",         "Classic remote-thread injection marker"),
    ("WriteProcessMemory",         "Direct process-write marker — use Nt API instead"),
    ("LoadLibraryA",               "LoadLibrary call exposed in string table"),
    ("LoadLibraryW",               "LoadLibraryW call exposed in string table"),
    ("VirtualAllocEx",             "VirtualAllocEx call exposed in string table"),
    ("SparkyCore",                 "Payload identity string not stripped from binary"),
    ("ManualMap",                  "Loader identity string not stripped from binary"),
    ("SparkyDllSuccess",           "Test harness string leaked into production binary"),
]

# Presence earns a warning (not a hard failure) — noteworthy but not instant-ban.
WARN_STRINGS: list[tuple[str, str]] = [
    ("ntdll.dll",                  "ntdll string visible — prefer hash-based resolution"),
    ("kernel32.dll",               "kernel32 string visible — prefer hash-based resolution"),
    ("NtWriteVirtualMemory",       "NT function name in plain text"),
    ("NtAllocateVirtualMemory",    "NT function name in plain text"),
    ("GetProcAddress",             "GetProcAddress in plain text — use custom resolver"),
    ("GetModuleHandleW",           "GetModuleHandle in plain text"),
    ("VirtualProtect",             "VirtualProtect string visible"),
    ("SparkyLoader",               "Loader name in plain text — consider stripping"),
    ("sparky.crt",                 "Certificate filename in binary — consider obfuscating"),
    ("sparky.key",                 "Key filename in binary — consider obfuscating"),
    ("sparky.connstr",             "Connection-string filename in binary"),
]

# ---------------------------------------------------------------------------
# IAT entries that are hard failures (no legitimate reason in a production loader)
# ---------------------------------------------------------------------------
HARD_FAIL_IMPORTS: list[tuple[str, str]] = [
    ("CreateRemoteThread",   "Never use this — use APC (QueueUserAPC) instead"),
    ("WriteProcessMemory",   "Use NtWriteVirtualMemory directly via ntdll"),
    ("VirtualAllocEx",       "Use NtAllocateVirtualMemory directly via ntdll"),
    ("LoadLibraryA",         "Do not import LoadLibrary — resolve all DLLs via LdrLoadDll"),
    ("LoadLibraryW",         "Do not import LoadLibraryW"),
    ("LoadLibraryExA",       "Do not import LoadLibraryExA"),
    ("LoadLibraryExW",       "Do not import LoadLibraryExW"),
]

WARN_IMPORTS: list[tuple[str, str]] = [
    ("GetProcAddress",       "Prefer custom export resolver based on PEB walk"),
    ("VirtualProtect",       "Prefer NtProtectVirtualMemory via ntdll"),
    ("CreateThread",         "Visible thread creation — consider APC-only flow"),
    ("OpenProcess",          "Direct OpenProcess call visible in IAT"),
    ("TerminateProcess",     "TerminateProcess in IAT — check if really needed"),
]

# ---------------------------------------------------------------------------
# Entropy thresholds
# ---------------------------------------------------------------------------
# Below this entropy a "should-be-encrypted" section is suspiciously predictable.
ENTROPY_LOW_THRESHOLD  = 3.5
# Above this entropy in a normal code section suggests a packer.
ENTROPY_HIGH_THRESHOLD = 7.9

# ---------------------------------------------------------------------------
# Minimal PE parser (pure Python, no external libs)
# ---------------------------------------------------------------------------

def _shannon_entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = [0] * 256
    for b in data:
        counts[b] += 1
    entropy = 0.0
    n = len(data)
    for c in counts:
        if c:
            p = c / n
            entropy -= p * math.log2(p)
    return entropy


class PEFile:
    """
    Minimal PE reader.  Parses enough of the PE format to:
      - Enumerate sections (name + raw bytes)
      - Enumerate IAT entries (DLL name + function name)
    Raises ValueError on malformed input.
    """

    def __init__(self, data: bytes) -> None:
        self._data = data
        self._parse()

    # ------------------------------------------------------------------
    def _parse(self) -> None:
        d = self._data

        # DOS header
        if len(d) < 64 or struct.unpack_from("<H", d, 0)[0] != 0x5A4D:
            raise ValueError("Not a PE file (bad DOS signature)")

        e_lfanew = struct.unpack_from("<I", d, 0x3C)[0]

        # NT headers
        if len(d) < e_lfanew + 4:
            raise ValueError("Truncated PE (NT header past EOF)")
        if struct.unpack_from("<I", d, e_lfanew)[0] != 0x00004550:
            raise ValueError("Not a PE file (bad NT signature)")

        coff_off = e_lfanew + 4
        machine, num_sections, _, _, _, opt_size, _chars = struct.unpack_from(
            "<HHIIIHH", d, coff_off)
        self.machine = machine

        opt_off  = coff_off + 20
        opt_magic = struct.unpack_from("<H", d, opt_off)[0]
        if opt_magic == 0x020B:      # PE32+
            self._is_pe64 = True
            dd_off = opt_off + 112   # offset to DataDirectory in PE32+
        elif opt_magic == 0x010B:    # PE32
            self._is_pe64 = False
            dd_off = opt_off + 96
        else:
            raise ValueError(f"Unknown OptionalHeader magic: 0x{opt_magic:04X}")

        # Import directory (entry 1)
        imp_rva, imp_size = struct.unpack_from("<II", d, dd_off + 8)

        # Section headers
        sect_off = opt_off + opt_size
        self.sections: list[dict] = []
        for i in range(num_sections):
            o = sect_off + i * 40
            name       = d[o:o+8].rstrip(b"\x00").decode("ascii", errors="replace")
            vsize      = struct.unpack_from("<I", d, o+8)[0]
            rva        = struct.unpack_from("<I", d, o+12)[0]
            raw_size   = struct.unpack_from("<I", d, o+16)[0]
            raw_offset = struct.unpack_from("<I", d, o+20)[0]
            raw_end    = raw_offset + raw_size
            self.sections.append({
                "name":       name,
                "vsize":      vsize,
                "rva":        rva,
                "raw_offset": raw_offset,
                "raw_size":   raw_size,
                "data":       d[raw_offset:raw_end] if raw_end <= len(d) else b"",
            })

        # Build a simple RVA → file-offset resolver using section table.
        self._imp_rva  = imp_rva
        self._imp_size = imp_size
        self._imports: list[dict] = []
        if imp_rva and imp_size:
            self._parse_imports()

    # ------------------------------------------------------------------
    def _rva_to_offset(self, rva: int) -> int | None:
        for s in self.sections:
            if s["rva"] <= rva < s["rva"] + s["raw_size"]:
                return s["raw_offset"] + (rva - s["rva"])
        return None

    def _read_cstr(self, offset: int, max_len: int = 256) -> str:
        end = self._data.find(b"\x00", offset, offset + max_len)
        if end < 0:
            end = offset + max_len
        return self._data[offset:end].decode("ascii", errors="replace")

    # ------------------------------------------------------------------
    def _parse_imports(self) -> None:
        """Parse the Import Directory Table and populate self._imports."""
        d   = self._data
        off = self._rva_to_offset(self._imp_rva)
        if off is None:
            return

        # Each IMAGE_IMPORT_DESCRIPTOR is 20 bytes.
        while True:
            if off + 20 > len(d):
                break
            orig_first_thunk, ts, fwd, name_rva, first_thunk = struct.unpack_from(
                "<IIIII", d, off)
            off += 20
            # All-zero descriptor marks the end.
            if not name_rva and not first_thunk:
                break

            dll_name_off = self._rva_to_offset(name_rva)
            dll_name = self._read_cstr(dll_name_off) if dll_name_off is not None else "?"

            # Prefer OriginalFirstThunk for function names; fall back to FirstThunk.
            thunk_rva = orig_first_thunk or first_thunk
            thunk_off = self._rva_to_offset(thunk_rva) if thunk_rva else None
            if thunk_off is None:
                continue

            ptr_size = 8 if self._is_pe64 else 4
            pack_fmt  = "<Q" if self._is_pe64 else "<I"
            ordinal_flag = 0x8000_0000_0000_0000 if self._is_pe64 else 0x8000_0000

            while thunk_off + ptr_size <= len(d):
                thunk_val = struct.unpack_from(pack_fmt, d, thunk_off)[0]
                thunk_off += ptr_size
                if thunk_val == 0:
                    break
                if thunk_val & ordinal_flag:
                    fn_name = f"ordinal#{thunk_val & 0xFFFF}"
                else:
                    hint_off = self._rva_to_offset(thunk_val & 0x7FFF_FFFF)
                    if hint_off is None:
                        continue
                    fn_name = self._read_cstr(hint_off + 2)  # skip 2-byte hint

                self._imports.append({"dll": dll_name.lower(), "func": fn_name})

    @property
    def imports(self) -> list[dict]:
        return self._imports


# ---------------------------------------------------------------------------
# Scan functions
# ---------------------------------------------------------------------------

def scan_strings(data: bytes) -> tuple[list[str], list[str]]:
    """
    Returns (failures, warnings) — each entry is a human-readable message.
    Checks both ASCII and UTF-16LE representations.
    """
    failures: list[str] = []
    warnings: list[str] = []

    # Decode the whole binary as Latin-1 so every byte is preserved.
    text_ascii = data.decode("latin-1")

    # Also check UTF-16LE (WCHAR strings from Windows API calls).
    try:
        text_utf16 = data.decode("utf-16-le", errors="replace")
    except Exception:
        text_utf16 = ""

    def _contains(pattern: str) -> bool:
        return pattern in text_ascii or pattern in text_utf16

    for pattern, reason in HARD_FAIL_STRINGS:
        if _contains(pattern):
            failures.append(f'"{pattern}" — {reason}')

    for pattern, reason in WARN_STRINGS:
        if _contains(pattern):
            warnings.append(f'"{pattern}" — {reason}')

    return failures, warnings


def scan_imports(pe: PEFile) -> tuple[list[str], list[str]]:
    failures: list[str] = []
    warnings: list[str] = []

    imported_funcs = {imp["func"] for imp in pe.imports}

    for fn, reason in HARD_FAIL_IMPORTS:
        if fn in imported_funcs:
            failures.append(f"{fn} in IAT — {reason}")

    for fn, reason in WARN_IMPORTS:
        if fn in imported_funcs:
            warnings.append(f"{fn} in IAT — {reason}")

    return failures, warnings


def scan_entropy(pe: PEFile) -> tuple[list[str], list[str]]:
    """
    Warns when section entropy is suspiciously low (predictable "encryption")
    or suspiciously high in a code section (packer/obfuscator).
    """
    failures: list[str] = []
    warnings: list[str] = []

    for s in pe.sections:
        if not s["data"]:
            continue
        e = _shannon_entropy(s["data"])
        name = s["name"] or "(unnamed)"

        # Code sections should not be packed to near-8.0 entropy.
        if name in (".text", "CODE") and e > ENTROPY_HIGH_THRESHOLD:
            warnings.append(
                f"Section '{name}' entropy {e:.3f} > {ENTROPY_HIGH_THRESHOLD:.1f} "
                f"— packed/obfuscated code section (may trigger sandbox detonation)"
            )

        # Data sections that are meant to be encrypted but have low entropy.
        if name in (".rdata", ".data", ".rsrc") and e < ENTROPY_LOW_THRESHOLD:
            warnings.append(
                f"Section '{name}' entropy {e:.3f} < {ENTROPY_LOW_THRESHOLD:.1f} "
                f"— predictable data; if this section is encrypted the cipher is weak"
            )

        # Any section that claims to be encrypted/packed (often named .enc or .sparky)
        # but has low entropy is a false flag.
        if name.startswith(".enc") and e < ENTROPY_LOW_THRESHOLD:
            failures.append(
                f"Section '{name}' entropy {e:.3f} is too low for an encrypted section — "
                f"encryption is ineffective or not applied"
            )

    return failures, warnings


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> int:
    _enable_ansi()

    parser = argparse.ArgumentParser(
        description="Static 'Flag Fiction' scanner for SparkyLoader.exe")
    parser.add_argument(
        "binary",
        nargs="?",
        default=None,
        help="PE binary to scan (default: ../bin/SparkyLoader.exe)",
    )
    parser.add_argument(
        "--fail-on-warn",
        action="store_true",
        help="Exit 1 when warnings are found, not just hard failures",
    )
    args = parser.parse_args()

    if args.binary:
        binary_path = Path(args.binary).resolve()
    else:
        script_dir  = Path(__file__).resolve().parent
        binary_path = (script_dir / ".." / "bin" / "SparkyLoader.exe").resolve()

    if not binary_path.exists():
        print(f"\033[91m[ERROR]\033[0m Binary not found: {binary_path}")
        return 2

    print(f"{_BOLD}Sparky Static Scanner{_RESET}")
    print(f"  Target : {binary_path}")
    print(f"  Size   : {binary_path.stat().st_size:,} bytes\n")

    data = binary_path.read_bytes()

    try:
        pe = PEFile(data)
    except ValueError as e:
        print(f"\033[91m[ERROR]\033[0m Failed to parse PE: {e}")
        return 2

    total_failures = 0
    total_warnings = 0

    # -- String scan ----------------------------------------------------------
    print(f"{_BOLD}[1/3] Plaintext String Check{_RESET}")
    s_fail, s_warn = scan_strings(data)
    if not s_fail and not s_warn:
        print(_ok("No sensitive strings found."))
    for msg in s_fail:
        print(_bad(msg))
    for msg in s_warn:
        print(_warn(msg))
    total_failures += len(s_fail)
    total_warnings += len(s_warn)

    # -- IAT scan -------------------------------------------------------------
    print(f"\n{_BOLD}[2/3] Import Address Table (IAT) Check{_RESET}")
    if pe.imports:
        print(_info(f"{len(pe.imports)} imported symbols across "
                    f"{len({i['dll'] for i in pe.imports})} DLL(s)"))
    else:
        print(_info("No imports found (could be manually resolved or binary is packed)"))

    i_fail, i_warn = scan_imports(pe)
    if not i_fail and not i_warn:
        print(_ok("No high-risk imports in IAT."))
    for msg in i_fail:
        print(_bad(msg))
    for msg in i_warn:
        print(_warn(msg))
    total_failures += len(i_fail)
    total_warnings += len(i_warn)

    # -- Entropy scan ---------------------------------------------------------
    print(f"\n{_BOLD}[3/3] Section Entropy Analysis{_RESET}")
    for s in pe.sections:
        if not s["data"]:
            continue
        e = _shannon_entropy(s["data"])
        bar_len  = int(e / 8.0 * 30)
        bar_fill = "█" * bar_len + "░" * (30 - bar_len)
        colour   = _GREEN if 4.0 <= e <= 7.5 else _YELLOW
        print(f"  {colour}{s['name']:10}{_RESET}  "
              f"[{bar_fill}] {e:.3f} bits/byte  "
              f"({s['raw_size']:>8,} bytes raw)")

    e_fail, e_warn = scan_entropy(pe)
    for msg in e_fail:
        print(_bad(msg))
    for msg in e_warn:
        print(_warn(msg))
    total_failures += len(e_fail)
    total_warnings += len(e_warn)

    # -- Summary --------------------------------------------------------------
    print(f"\n{_BOLD}{'─'*60}{_RESET}")
    print(f"  Hard failures : {total_failures}")
    print(f"  Warnings      : {total_warnings}")
    print(f"{_BOLD}{'─'*60}{_RESET}")

    if total_failures > 0:
        print(_bad(f"FAIL — {total_failures} hard indicator(s) found."))
        print(_info("Fix these before shipping: they will trigger AC ban-on-sight rules."))
        return 1

    if total_warnings > 0 and args.fail_on_warn:
        print(_warn(f"FAIL (--fail-on-warn) — {total_warnings} warning(s) found."))
        return 1

    if total_warnings > 0:
        print(_warn(f"PASS (with {total_warnings} warning(s)) — review the warnings above."))
    else:
        print(_ok("PASS — no Flag Fiction detected."))

    return 0


if __name__ == "__main__":
    sys.exit(main())
