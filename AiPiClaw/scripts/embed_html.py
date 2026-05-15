#!/usr/bin/env python3
"""
embed_html.py — Convert HTML files to C string headers for embedded firmware.

Reads HTML files from specified paths, escapes them as static C string arrays,
and writes C header files ready for #include in the firmware build.

Usage:
  python3 scripts/embed_html.py <input.html> <output.h> <varname>
  python3 scripts/embed_html.py \\
      /mnt/d/.../config_ui.html  src/gateway/config_ui.h   CONFIG_UI_HTML \\
      /mnt/d/.../web_ui.html     src/gateway/web_ui.h      WEB_UI_HTML

The output file defines:
  static const char <varname>[] = "...";
with proper C string escaping (\n, \r, \\, \", etc.).
"""

import os
import sys


def escape_c_string(text: str) -> str:
    """Escape a string for use as a C string literal."""
    result = []
    for ch in text:
        if ch == '\n':
            result.append('\\n"\n    "')
        elif ch == '\r':
            result.append('\\r')
        elif ch == '\\':
            result.append('\\\\')
        elif ch == '"':
            result.append('\\"')
        elif ch == '\t':
            result.append('\\t')
        elif 32 <= ord(ch) < 127:
            result.append(ch)
        else:
            result.append(f'\\x{ord(ch):02x}')
    return ''.join(result)


def embed_html(input_path: str, output_path: str, var_name: str) -> None:
    """Convert an HTML file to a C header with an embedded string constant."""
    if not os.path.isfile(input_path):
        print(f"[embed] ERROR: input not found: {input_path}")
        sys.exit(1)

    with open(input_path, 'r', encoding='utf-8') as f:
        html = f.read()

    if not html:
        print(f"[embed] ERROR: empty input: {input_path}")
        sys.exit(1)

    escaped = escape_c_string(html)

    # Build comment header
    base_name = os.path.basename(input_path)
    header_guard = f"_{os.path.basename(output_path).upper().replace('.', '_')}_"

    content = (
        f"#ifndef {header_guard}\n"
        f"#define {header_guard}\n"
        f"\n"
        f"/**\n"
        f" * @brief Embedded Web UI — auto-generated from {base_name}\n"
        f" * @note Do not edit manually. Regenerate with scripts/embed_html.py\n"
        f" */\n"
        f"static const char {var_name}[] =\n"
        f'    "{escaped}";\n'
        f"\n"
        f"#endif /* {header_guard} */\n"
    )

    # Ensure output directory exists
    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(content)

    size_kb = len(html) / 1024
    lines = html.count('\n')
    print(f"[embed] {base_name} → {output_path}: {len(html)} bytes ({size_kb:.1f} KB, {lines} lines)")


def main():
    args = sys.argv[1:]
    if len(args) < 3 or (len(args) % 3 != 0):
        print(__doc__)
        sys.exit(1)

    for i in range(0, len(args), 3):
        embed_html(args[i], args[i + 1], args[i + 2])


if __name__ == '__main__':
    main()
