#!/usr/bin/env python3
"""Generate compile_commands.json for clangd and sourcekit-lsp."""

import glob
import json
import os


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    cflags = [
        "clang",
        "-std=c99",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic",
        "-Iinclude",
        "-Ivendor/yyjson",
        "-Ivendor/tomlc99",
    ]

    patterns = [
        os.path.join(root, "src", "**", "*.c"),
        os.path.join(root, "vendor", "**", "*.c"),
        os.path.join(root, "tests", "**", "*.c"),
    ]

    files = []
    for pat in patterns:
        files.extend(glob.glob(pat, recursive=True))

    entries = []
    for f in sorted(files):
        rel_path = os.path.relpath(f, root)
        entries.append(
            {
                "directory": root,
                "arguments": cflags + ["-c", rel_path],
                "file": rel_path,
            }
        )

    out_path = os.path.join(root, "compile_commands.json")
    with open(out_path, "w") as out:
        json.dump(entries, out, indent=2)

    print(f"[+] Generated {out_path} with {len(entries)} translation units.")


if __name__ == "__main__":
    main()
