"""Check project include paths against CMake's actual include directories.

Usage: python python/check_includes.py build/compile_commands.json
Checks exact spelling even on a case-insensitive filesystem. Conditional quoted
includes are checked too, so inactive MCU branches cannot hide broken paths.
The compiler remains responsible for system headers and preprocessor semantics.
"""

import argparse
import json
import re
import shlex
from pathlib import Path


def exact_file(base, relative):
    """Resolve each component using directory entries, preserving case checks."""
    current = base
    for part in Path(relative).parts:
        if part == "..":
            current = current.parent
        elif part != ".":
            if not current.is_dir() or part not in {p.name for p in current.iterdir()}:
                return False
            current = current / part
    return current.is_file()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compile_commands", type=Path)
    args = parser.parse_args()
    entries = json.loads(args.compile_commands.read_text(encoding="utf-8"))
    root = Path(__file__).resolve().parent.parent
    include_dirs = set()
    errors = []
    for entry in entries:
        directory = Path(entry["directory"])
        source = Path(entry["file"])
        if not source.is_absolute():
            source = directory / source
        if not source.is_file():
            errors.append(f"Missing build source: {source}")
        tokens = entry.get("arguments") or shlex.split(entry["command"])
        for index, token in enumerate(tokens):
            if token == "-I":
                name = tokens[index + 1]
            elif token.startswith("-I"):
                name = token[2:]
            else:
                continue
            path = Path(name)
            include_dirs.add((directory / path).resolve())
    for directory in sorted(include_dirs):
        if not directory.is_dir():
            errors.append(f"Missing include directory: {directory}")

    files = sorted(p for folder in ("src", "USB_Device")
                   for p in (root / folder).rglob("*") if p.suffix in (".c", ".h"))
    count = 0
    for source in files:
        text = source.read_text(encoding="utf-8-sig")
        # Preserve line numbers while removing comments.
        text = re.sub(r"/\*.*?\*/|//[^\n]*", lambda m: "\n" * m[0].count("\n"),
                      text, flags=re.S)
        for line, content in enumerate(text.splitlines(), 1):
            match = re.match(r'\s*#\s*include\s*"([^"]+)"', content)
            if not match:
                continue
            name = match[1]
            count += 1
            if not any(exact_file(base, name) for base in (source.parent, *include_dirs)):
                errors.append(f'{source.relative_to(root)}:{line}: missing or wrong-case include "{name}"')
    if errors:
        raise SystemExit("\n".join(errors))
    print(f"OK: {count} quoted includes in {len(files)} project files; {len(entries)} build sources.")


if __name__ == "__main__":
    main()
