#!/usr/bin/env python3
"""
Compare an annotated source file against its original, ignoring annotation markers.

Usage:
    python3 compare_annotated.py <annotated_file> <original_file>

The annotated file is expected to have lines prefixed with a single uppercase
letter and a space (e.g. "I ", "C ", "D ", "E ", "T ", "M ") or a bare "-"
for blank/separator lines. These prefixes are stripped before comparison.
"""

import sys


def strip_annotation(line: str) -> str:
    """Remove a leading annotation marker from a line, if present."""
    if len(line) >= 2 and line[0].isupper() and line[1] == ' ':
        return line[2:]
    if line.rstrip('\n') == '-':
        return '\n'
    if line.startswith('- '):
        return line[2:]
    return line


def load_and_strip(path: str) -> list[str]:
    with open(path) as f:
        return [strip_annotation(line) for line in f]


def compare(annotated_path: str, original_path: str) -> bool:
    annotated = load_and_strip(annotated_path)
    original = []
    with open(original_path) as f:
        original = list(f)

    # Normalise: strip trailing whitespace and blank lines so that
    # cosmetic differences (trailing spaces, extra blank lines at EOF)
    # don't produce false negatives.
    def normalise(lines):
        stripped = [l.rstrip() for l in lines]
        # Drop leading/trailing blank lines
        while stripped and stripped[0] == '':
            stripped.pop(0)
        while stripped and stripped[-1] == '':
            stripped.pop()
        return stripped

    a_norm = normalise(annotated)
    o_norm = normalise(original)

    if a_norm == o_norm:
        print("OK: files match after stripping annotations.")
        return True

    # Report all differing lines.
    max_len = max(len(a_norm), len(o_norm))
    differences = []
    for i in range(max_len):
        a_line = a_norm[i] if i < len(a_norm) else '<missing>'
        o_line = o_norm[i] if i < len(o_norm) else '<missing>'
        if a_line != o_line:
            differences.append((i + 1, a_line, o_line))

    print(f"MISMATCH: {len(differences)} differing line(s).\n")
    for lineno, a_line, o_line in differences:
        print(f"  Line {lineno}:")
        print(f"    annotated : {repr(a_line)}")
        print(f"    original  : {repr(o_line)}")
    return False


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <annotated_file> <original_file>")
        sys.exit(1)
    ok = compare(sys.argv[1], sys.argv[2])
    sys.exit(0 if ok else 1)
