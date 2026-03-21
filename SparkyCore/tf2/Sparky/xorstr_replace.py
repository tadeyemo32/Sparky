#!/usr/bin/env python3
"""
Wraps plain string literals in C++ source files with XS().
Skips: empty strings, prefixed strings (L/u/U/u8/R), preprocessor lines,
       strings in line/block comments, and strings in function default args.
"""

import os
import sys


def transform(text: str) -> str:
    out = []
    i = 0
    n = len(text)
    in_block_comment = False

    # Returns the text of the current line up to position `pos` in `out`
    def current_line_text():
        j = len(out) - 1
        while j >= 0 and out[j] != '\n':
            j -= 1
        return ''.join(out[j + 1:]).lstrip()

    while i < n:
        # ── block comment ────────────────────────────────────────────────
        if in_block_comment:
            if text[i] == '*' and i + 1 < n and text[i + 1] == '/':
                out.append('*/')
                i += 2
                in_block_comment = False
            else:
                out.append(text[i])
                i += 1
            continue

        if text[i] == '/' and i + 1 < n and text[i + 1] == '*':
            out.append('/*')
            i += 2
            in_block_comment = True
            continue

        # ── line comment ─────────────────────────────────────────────────
        if text[i] == '/' and i + 1 < n and text[i + 1] == '/':
            while i < n and text[i] != '\n':
                out.append(text[i])
                i += 1
            continue

        # ── character literal ─────────────────────────────────────────────
        if text[i] == "'":
            out.append("'")
            i += 1
            while i < n:
                c = text[i]
                out.append(c)
                i += 1
                if c == '\\' and i < n:
                    out.append(text[i])
                    i += 1
                elif c == "'":
                    break
            continue

        # ── string literal ────────────────────────────────────────────────
        if text[i] == '"':
            # Detect prefix by looking back in output
            j = len(out) - 1
            prefix = ''
            while j >= 0 and out[j] in 'LuUR8':
                prefix = out[j] + prefix
                j -= 1

            has_prefix = bool(prefix)
            is_raw = 'R' in prefix
            is_preprocessor = current_line_text().startswith('#')

            str_begin = len(out)  # index in `out` where the string starts
            out.append('"')
            i += 1

            if is_raw:
                # R"delim(content)delim"
                delim_chars = []
                while i < n and text[i] != '(':
                    delim_chars.append(text[i])
                    out.append(text[i])
                    i += 1
                if i < n:
                    out.append('(')
                    i += 1
                end_seq = ')' + ''.join(delim_chars) + '"'
                while i < n:
                    out.append(text[i])
                    i += 1
                    tail = ''.join(out[-len(end_seq):])
                    if tail == end_seq:
                        break
            else:
                while i < n:
                    c = text[i]
                    out.append(c)
                    i += 1
                    if c == '\\' and i < n:
                        out.append(text[i])
                        i += 1
                    elif c == '"':
                        break

            string_literal = ''.join(out[str_begin:])

            # Decide whether to wrap
            if has_prefix or is_raw or is_preprocessor or string_literal == '""':
                continue  # already appended as-is

            # Check if already inside XS(  — avoid double-wrapping
            before = ''.join(out[:str_begin]).rstrip()
            if before.endswith('XS('):
                continue

            # Detect function-default-argument pattern:
            #   = "..."  followed by  )  or  ,  (parameter default)
            # We look at the token immediately before the string.
            before_stripped = before.rstrip()
            if before_stripped.endswith('='):
                # Peek ahead past the closing quote to see if next non-space
                # char is ')' or ',' — if so, this is a default arg, skip.
                k = i
                while k < n and text[k] in ' \t':
                    k += 1
                if k < n and text[k] in '),':
                    continue  # leave default-arg strings unwrapped

            # Wrap: insert XS( before the string and ) after
            out.insert(str_begin, 'XS(')
            out.append(')')
            continue

        # ── everything else ───────────────────────────────────────────────
        out.append(text[i])
        i += 1

    return ''.join(out)


def process_file(path: str) -> bool:
    try:
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            original = f.read()
    except Exception as e:
        print(f'  ERROR reading {path}: {e}')
        return False

    result = transform(original)
    if result == original:
        return False

    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(result)
    return True


def main():
    src_dir = sys.argv[1] if len(sys.argv) > 1 else '.'
    extensions = {'.cpp', '.h'}

    changed = 0
    skipped = 0

    for root, dirs, files in os.walk(src_dir):
        # Skip third-party include directories
        if os.sep + 'include' + os.sep in root or root.endswith(os.sep + 'include'):
            dirs[:] = [d for d in dirs if d not in ('ImGui', 'MinHook', 'freetype')]
        for fname in files:
            if os.path.splitext(fname)[1] in extensions:
                path = os.path.join(root, fname)
                if process_file(path):
                    print(f'  changed  {path}')
                    changed += 1
                else:
                    skipped += 1

    print(f'\nDone: {changed} files changed, {skipped} files unchanged.')


if __name__ == '__main__':
    main()
