# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract unchanged C++ functions for SDK regression fixtures."""
import re


def function(raw: bytes, signature: bytes):
    """Return one exact function, ignoring braces in comments and literals."""
    matches = [m.start() for m in re.finditer(re.escape(signature), raw)]
    if len(matches) != 1:
        raise ValueError((signature.decode(), len(matches)))
    start = matches[0]
    brace = raw.index(b'{', start)
    i, depth, state = brace, 0, 'code'
    while i < len(raw):
        a, b = raw[i:i + 1], raw[i:i + 2]
        if state == 'line':
            if a == b'\n':
                state = 'code'
        elif state == 'block':
            if b == b'*/':
                state = 'code'
                i += 1
        elif state in ('"', "'"):
            if a == b'\\':
                i += 1
            elif a.decode('latin1') == state:
                state = 'code'
        elif b == b'//':
            state = 'line'
            i += 1
        elif b == b'/*':
            state = 'block'
            i += 1
        elif a in (b'"', b"'"):
            state = a.decode('latin1')
        elif a == b'{':
            depth += 1
        elif a == b'}':
            depth -= 1
            if depth == 0:
                return start, i + 1, raw[start:i + 1]
        i += 1
    raise ValueError('Unterminated function: ' + signature.decode())
