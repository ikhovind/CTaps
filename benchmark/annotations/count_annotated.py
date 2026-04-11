#!/usr/bin/env python3
"""
Count the lines of code in each category in an annotated source file.

Usage:
    python3 count_annotated.py <annotated_file>
"""

import sys

def get_annotation(line: str) -> str:
    """Remove a leading annotation marker from a line, if present."""
    if len(line) >= 2 and line[0].isupper() and line[1] == ' ':
        return line[0]
    return None

def load_and_count(path: str) -> dict[str, int]:
    res = {}
    with open(path) as f:
        for line in f:
            annotation = get_annotation(line)
            if annotation:
                if annotation not in res:
                    res[annotation] = 0
                res[annotation] += 1
    return res

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <annotated_file>")
        sys.exit(1)
    res = load_and_count(sys.argv[1])
    sum = 0
    for key, val in res.items():
        print(f"{key}: {val}")
        sum += val
    print(f"sum: {sum}")

