#!/usr/bin/env python3
"""
Auto-doc: Add /* @brief @param @return */ to undocumented functions
and /*< 说明 */ to undocumented struct fields in .c/.h files.
Uses Python file I/O for reliable editing.
"""

import os
import re
import json
import glob

SRC_DIR = "/home/seahi/workspase/AiPiClaw/AiPiClaw/src"
LOG_FILE = "/mnt/d/Users/Seahi/Desktop/agent-reports/firmware-dev-report.md"

# ─── C keyword sets ────────────────────────────────────────────────

_CONTROL_KW = {
    'if', 'while', 'for', 'switch', 'else', 'do',
    'return', 'sizeof', 'goto', 'break', 'continue',
    'typedef', 'struct', 'enum', 'union', 'case', 'default',
    'define', 'include', 'ifdef', 'ifndef', 'endif', 'undef',
    'defined', 'pragma', 'error', 'warning', 'line', 'elif',
    '_Pragma',
}

_TYPE_KW_SET = {
    'int', 'void', 'char', 'float', 'double', 'long', 'short',
    'unsigned', 'signed', 'const', 'volatile', 'extern', 'static',
    'inline', 'register', 'restrict', 'bool', '_Bool', 'size_t',
    'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t',
    'int8_t', 'int16_t', 'int32_t', 'int64_t',
    'intptr_t', 'uintptr_t', 'ptrdiff_t', 'wchar_t',
    'time_t', 'clock_t', 'FILE', 'ssize_t', 'off_t',
    'struct', 'enum', 'union',
}

FUNC_HEAD_RE = re.compile(
    r'^(\s*)'  # 1: indent
    r'('  # 2: type+qualifiers
    r'(?:const\s+|volatile\s+|extern\s+|static\s+|inline\s+|register\s+|restrict\s+)*'
    r'(?:\w+(?:\s+\w+)*(?:\s*[*])?\s+)+'
    r')'
    r'(?!if|while|for|switch|else|do|return|sizeof|case|defined)\b'
    r'(\w+)'  # 3: function name
    r'\s*\('
    r'(.*)$',  # 4: rest after (
    re.MULTILINE
)

STRUCT_MEMBER_RE = re.compile(
    r'^\s*'
    r'(?:const\s+)?'
    r'(?:\w+(?:\s+\w+)*)'
    r'\s+'
    r'(\w+)'
    r'(?:\s*\[[^\]]*\])?'
    r'\s*;\s*$'
)

HAS_TRAILING_DOC_RE = re.compile(r'/\*[*]?<\s*')

# Section comments that should NOT be treated as function docs
SECTION_COMMENT_RE = re.compile(
    r'^\s*/\*+\s*[=\-─━═▬]'
)


def _log(msg):
    ts = __import__('datetime').datetime.now().strftime('%H:%M:%S')
    with open(LOG_FILE, 'a') as f:
        f.write(f"\n- [{ts}] {msg}\n")
    print(f"[{ts}] {msg}")


# ─── Type prefix check ─────────────────────────────────────────────

def _has_type_prefix(s):
    tokens = s.split()
    if not tokens:
        return False
    first = tokens[0].lstrip('(')
    if first in _TYPE_KW_SET or first.rstrip('*') in _TYPE_KW_SET:
        return True
    if first.startswith('axk_') and first.endswith('_t'):
        return True
    if first.startswith('AXK_') and first.endswith('_t'):
        return True
    return False


# ─── Collect full signature across lines ───────────────────────────

def _get_full_sig(lines, start):
    """Read signature until parens balance. Returns (sig_string, end_idx)."""
    sig = lines[start].rstrip()
    opens = sig.count('(')
    closes = sig.count(')')
    j = start
    while opens > closes and j + 1 < len(lines):
        j += 1
        nl = lines[j]
        opens += nl.count('(')
        closes += nl.count(')')
        sig += ' ' + nl.strip()
    return sig, j


# ─── Check for existing function docs ──────────────────────────────

def _has_func_doc(lines, func_line_idx, max_lookback=20):
    """
    Check if there is a function doc comment before the function.
    Returns True only for DOXYGEN-style docs (containing @brief or @param),
    NOT for decorative section comments.
    """
    for i in range(func_line_idx - 1, max(-1, func_line_idx - max_lookback - 1), -1):
        if i < 0:
            return False
        line = lines[i].strip()
        if not line:
            continue

        s = line.lstrip()

        # Skip preprocessor
        if s.startswith('#'):
            return False

        if s.startswith(('//', '///')):
            # Line comment counts if it mentions @brief
            if '@brief' in s or '@param' in s or '@return' in s or '@note' in s:
                return True
            # Otherwise treat as general comment (could be section label)
            continue

        if s.startswith('/*'):
            # Check if this is a Doxygen block comment
            if '@brief' in line or '@param' in line or '@return' in line or '@note' in line:
                return True
            # Check if this is a multi-line /** block
            if '/**' in line and '@' in line:
                return True
            # Section comments like /* ==== ... ==== */ or /* ── ... ── */ are NOT function docs
            if SECTION_COMMENT_RE.match(line):
                return False  # Not a function doc, continue looking
            # Any other /* ... */ block - could be a non-Doxygen doc
            # Check if it's a single-line block comment
            if '*/' in s and not s.startswith('/**'):
                # Single-line non-Doxygen comment
                return False
            # Multi-line block comment, keep looking through its continuation lines
            continue

        if s.startswith('*'):
            # Continuation of a multi-line /* */ comment
            # Check if this continuation has Doxygen keywords
            if '@brief' in line or '@param' in line:
                return True
            continue

        # Hit code or preprocessor -> no doc
        return False

    return False


# ─── Extract param names ───────────────────────────────────────────

def _extract_params(lines, func_line_idx):
    """Extract parameter names from function signature."""
    sig, _ = _get_full_sig(lines, func_line_idx)

    m = re.search(r'\(([^)]*)\)', sig)
    if not m:
        return []

    param_str = m.group(1).strip()
    if not param_str or param_str == 'void':
        return []

    non_param = {'const', 'volatile', 'unsigned', 'signed', 'long', 'short',
                 'int', 'void', 'char', 'float', 'double', 'size_t', 'bool',
                 'restrict', 'inline', 'struct', 'enum', 'union', 'extern'}

    params = []
    for p in param_str.split(','):
        p = p.strip()
        if not p:
            continue

        # Function pointer: type (*name)(...)
        fp = re.search(r'\(\s*\*\s*(\w+)', p)
        if fp:
            params.append(fp.group(1))
            continue

        tokens = p.split()
        if tokens:
            last = tokens[-1].lstrip('*').rstrip(']')
            if '[' in last:
                last = last[:last.index('[')]
            # Clean trailing junk
            last = re.sub(r'[^a-zA-Z0-9_]', '', last)
            if last and last not in non_param:
                params.append(last)

    return params


# ─── Generate function comment ─────────────────────────────────────

def _gen_comment(func_name, params, indent='', is_void_return=False):
    c = f"{indent}/* @brief TODO: 描述{func_name}的功能"
    for p in params:
        c += f" @param {p} TODO: 描述{p}"
    if is_void_return:
        c += " @return 无返回值 */"
    else:
        c += " @return 0成功, -1失败 */"
    return c


# ─── Scan for modifications ────────────────────────────────────────

def scan_file(filepath):
    """
    Scan a file for undocumented functions and struct fields.
    Returns list of (line_idx, old_line, new_line).
    """
    with open(filepath, 'r', newline='') as f:
        content = f.read()
    # Preserve original line endings
    has_crlf = '\r\n' in content
    raw_lines = content.splitlines(keepends=True)
    # Normalize: work with stripped lines internally
    lines = [l.rstrip('\n\r') for l in raw_lines]

    mods = []

    # ── PASS 1: Functions ──
    i = 0
    while i < len(lines):
        line = lines[i]

        # Quick reject
        if not line or line.lstrip().startswith(('#', '/*', '*', '//', '/*<', '///')):
            i += 1
            continue

        if not _has_type_prefix(line):
            i += 1
            continue

        m = FUNC_HEAD_RE.match(line)
        if not m:
            i += 1
            continue

        func_name = m.group(3)
        if func_name in _CONTROL_KW:
            i += 1
            continue

        indent = m.group(1)

        # Verify: must have ){ or ); after collecting full signature
        sig, end_j = _get_full_sig(lines, i)

        # Check current line or next non-comment line for ){ or );
        if not re.search(r'\)\s*[\{;]', sig):
            # Check next line
            nxt = end_j + 1
            found_brace = False
            while nxt < len(lines):
                nl = lines[nxt].strip()
                if not nl:
                    nxt += 1
                    continue
                if nl.startswith(('/*', '*', '//')):
                    nxt += 1
                    continue
                if nl.startswith('{') or nl == ';' or nl.startswith(';'):
                    found_brace = True
                break
            if not found_brace:
                i += 1
                continue

        # Check existing doc
        if _has_func_doc(lines, i):
            i += 1
            continue

        params = _extract_params(lines, i)
        # Detect return type: check if the first type token is 'void'
        first_token = lines[i].lstrip().split()[0] if lines[i].strip() else ''
        is_void_return = first_token == 'void' or (first_token == 'static' and len(lines[i].split()) > 1 and lines[i].split()[1] == 'void')
        comment = _gen_comment(func_name, params, indent, is_void_return)

        new_line = comment.rstrip('\n') + '\n' + line.rstrip('\n') + '\n'
        # Ensure consistent line ending
        if has_crlf:
            new_line = new_line.replace('\n', '\r\n')

        mods.append((i, raw_lines[i], new_line))
        i += 1

    # ── PASS 2: Struct fields ──
    in_struct = 0
    for i in range(len(lines)):
        line = lines[i]
        stripped = line.strip()

        # Track struct scope (simplified: count braces)
        struct_open = re.search(r'\bstruct\s*\{', stripped)
        if struct_open:
            in_struct += 1
            # Handle struct on same line as first member
            continue

        if '}' in stripped and in_struct > 0:
            in_struct -= 1
            continue

        if in_struct <= 0:
            continue

        # Skip comment/preprocessor lines
        if stripped.lstrip().startswith(('/*', '*', '//', '#', '///')):
            continue

        # Skip if already has trailing doc
        if HAS_TRAILING_DOC_RE.search(stripped):
            continue

        # Skip lines that look like nested struct/enum definitions
        if re.search(r'\b(struct|enum)\s', stripped):
            continue

        m = STRUCT_MEMBER_RE.match(stripped)
        if not m:
            continue

        member_name = m.group(1)
        # Insert /*< before semicolon
        old = raw_lines[i]
        new_base = lines[i].rstrip().rstrip(';') + ' /*< TODO: 描述' + member_name + ' */;\n'
        if has_crlf:
            new_base = new_base.replace('\n', '\r\n')
        mods.append((i, old, new_base))

    return mods


# ─── Process a file ────────────────────────────────────────────────

def process_file(filepath):
    """Process a single file. Returns (func_count, field_count, error_str)."""
    mods = scan_file(filepath)
    if not mods:
        return 0, 0, None

    with open(filepath, 'rb') as f:
        raw = f.read()
    has_crlf = b'\r\n' in raw

    with open(filepath, 'r', newline='') as f:
        lines = f.readlines()

    func_count = 0
    field_count = 0
    skipped = 0

    for idx, old, new in sorted(mods, key=lambda x: x[0], reverse=True):
        if idx >= len(lines):
            skipped += 1
            continue
        if lines[idx] != old:
            skipped += 1
            continue
        lines[idx] = new
        if '@brief' in new:
            func_count += 1
        else:
            field_count += 1

    # Write with original line endings
    mode = 'wb'
    out = ''.join(lines)
    if has_crlf:
        out_bytes = out.encode('utf-8').replace(b'\n', b'\r\n')
    else:
        out_bytes = out.encode('utf-8')

    with open(filepath, 'wb') as f:
        f.write(out_bytes)

    return func_count, field_count, skipped if skipped > 0 else None


# ─── Main ──────────────────────────────────────────────────────────

def main():
    _log(f"## Auto-doc Scan\n扫描目录: {SRC_DIR}")

    all_files = sorted(glob.glob(os.path.join(SRC_DIR, '**/*.c'), recursive=True) +
                       glob.glob(os.path.join(SRC_DIR, '**/*.h'), recursive=True))
    _log(f"找到 {len(all_files)} 个文件")

    total_func = 0
    total_field = 0
    processed = 0
    errors = []

    for fp in all_files:
        fname = os.path.relpath(fp, SRC_DIR)
        try:
            fc, fd, err = process_file(fp)
            if fc > 0 or fd > 0:
                _log(f"{fname}: +{fc} func +{fd} field")
            total_func += fc
            total_field += fd
            processed += 1
            if err:
                _log(f"  {fname}: skipped={err}")
        except Exception as e:
            errors.append(f"{fname}: {e}")
            _log(f"!! {fname} ERROR: {e}")

    _log(f"\n**完成**: {processed} 文件, +{total_func} 函数注释, +{total_field} 字段注释")

    if errors:
        _log(f"错误: {errors}")

    print(json.dumps({
        'processed': processed,
        'funcs': total_func,
        'fields': total_field,
        'errors': errors,
    }))


if __name__ == '__main__':
    main()
