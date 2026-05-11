#!/usr/bin/env python3
"""Analyze C/H files for missing Doxygen comments, output a structured report."""
import re, os, json, sys

SRC_DIR = os.path.dirname(os.path.abspath(__file__))

# Regex to match a function definition (return type, name, params)
FUNC_DEF_RE = re.compile(
    r'^(?!(?:static\s+)?(?:inline\s+)?(?:struct|typedef|enum|#|//|/\*|\*))\s*'
    r'(?:(?:static|inline|extern|const|volatile|unsigned|signed)\s+)*'
    r'(?:\w+(?:\s*\*?\s*)+)'
    r'(\w+)\s*\([^)]*\)\s*$',
    re.MULTILINE
)

# Simpler: match function signature line (return-type funcname(...) )
FUNC_SIG_RE = re.compile(
    r'^\s*(?:(?:static|inline|extern)\s+)*'
    r'(?:const\s+)?(?:unsigned\s+)?(?:volatile\s+)?'
    r'[\w\s\*]+?\s+(\w{3,})\s*\(([^)]*)\)\s*;?\s*$',
    re.MULTILINE
)

# Match struct fields
STRUCT_FIELD_RE = re.compile(
    r'^\s+([\w\s\*]+?)\s+(\w+)\s*;\s*(?:(?://|/\*\*\<|/\*\<)\s*(.*?)\s*)?(?:\*/)?\s*$',
    re.MULTILINE
)

# Match the doxygen comment block before a function
DOXY_PRE_RE = re.compile(
    r'/\*\*.*?\*/\s*$',
    re.DOTALL
)

DOXY_BRIEF_RE = re.compile(r'@brief\s+')

def has_doxygen_comment_before(lines, func_idx):
    """Check if there's a /** ... */ comment right before line func_idx."""
    if func_idx == 0:
        return False
    # Get joined text of the 10 lines before the function
    start = max(0, func_idx - 15)
    before_text = '\n'.join(lines[start:func_idx])
    # Look for /** ... */ pattern ending right before the function
    m = re.search(r'/\*\*[\s\S]*?\*/\s*$', before_text)
    if m:
        comment = m.group(0)
        if '@brief' in comment:
            return True
    return False

def extract_func_name(line):
    """Extract function name from a function signature line."""
    # Remove trailing semicolon
    line = line.rstrip(';').strip()
    # Match: return_type func_name(...)
    m = re.search(r'\b(\w{3,})\s*\(', line)
    if m:
        name = m.group(1)
        # Skip keywords
        if name in ('if', 'for', 'while', 'switch', 'return', 'sizeof', 'defined'):
            return None
        return name
    return None

def analyze_file(filepath):
    """Analyze a C/H file and return list of missing annotations."""
    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        content = f.read()
    lines = content.split('\n')
    
    issues = []
    
    # Find function definitions (not declarations/forward decls if we can distinguish)
    # We'll look for functions that have a body (open brace on same or next line)
    for i, line in enumerate(lines):
        stripped = line.strip()
        # Skip empty lines, comments, preprocessor
        if not stripped or stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*') or stripped.startswith('#'):
            continue
        
        # Check if this looks like a function definition (has { on same line or next line)
        func_name = extract_func_name(line)
        if not func_name:
            continue
        
        # Check if next line(s) contain {
        has_body = False
        for j in range(i, min(i + 3, len(lines))):
            if '{' in lines[j]:
                has_body = True
                break
        
        if not has_body:
            # Could still be a function - check if this is a multi-line signature
            # For now, skip single-line declarations (ending with ;)
            if stripped.endswith(';'):
                continue
            # Check next few lines for {
            combined = '\n'.join(lines[i:min(i+5, len(lines))])
            if '{' not in combined:
                continue
        
        if not has_doxygen_comment_before(lines, i):
            issues.append({
                'type': 'function',
                'name': func_name,
                'line': i + 1,
                'signature': stripped[:80]
            })
    
    # Find struct definitions and check field comments
    in_struct = False
    struct_name = ''
    struct_start = 0
    for i, line in enumerate(lines):
        stripped = line.strip()
        
        # Detect struct start
        if re.match(r'(?:typedef\s+)?struct\s*(?:\w+)?\s*\{', stripped):
            in_struct = True
            struct_name = re.search(r'struct\s+(\w+)', stripped)
            struct_name = struct_name.group(1) if struct_name else 'anonymous'
            struct_start = i + 1
            continue
        
        if in_struct:
            if stripped == '}' or stripped.startswith('} '):
                in_struct = False
                continue
            
            # Check if this is a field
            m = re.match(r'^\s+([\w\s\*]+?)\s+(\w+)\s*;', stripped)
            if m:
                field_type = m.group(1).strip()
                field_name = m.group(2)
                # Check if there's a trailing comment
                has_comment = bool(re.search(r'/\*\<|\*/\s*$|//.*$', stripped))
                if not has_comment:
                    issues.append({
                        'type': 'struct_field',
                        'struct': struct_name,
                        'field': field_name,
                        'field_type': field_type,
                        'line': i + 1,
                        'signature': stripped[:80]
                    })
    
    return issues

def main():
    all_issues = {}
    priority_order = ['platform', 'gateway', 'wifi', 'tools', 'cli', 'include', '.']
    
    for root_dir in priority_order:
        search_dir = os.path.join(SRC_DIR, root_dir) if root_dir != '.' else SRC_DIR
        if not os.path.isdir(search_dir):
            continue
        
        for dirpath, _, filenames in os.walk(search_dir):
            for fn in sorted(filenames):
                if fn.endswith('.c') or fn.endswith('.h'):
                    fullpath = os.path.join(dirpath, fn)
                    relpath = os.path.relpath(fullpath, SRC_DIR)
                    issues = analyze_file(fullpath)
                    if issues:
                        all_issues[relpath] = issues
    
    print(json.dumps(all_issues, indent=2, ensure_ascii=False))

if __name__ == '__main__':
    main()
