#!/usr/bin/env python3
"""Write private Wi-Fi configuration from two NUL-separated stdin values."""
import json
import os
from pathlib import Path
import re
import sys
import tempfile


def write_config(path, ssid, password):
    if len(ssid.encode('utf-8')) > 32 or any(ord(c)<32 for c in ssid):
        raise ValueError('Wi-Fi name must contain at most 32 UTF-8 bytes and no control characters.')
    length = len(password.encode('utf-8'))
    if any(ord(c)<32 for c in password):
        raise ValueError('Wi-Fi passwords must not contain control characters.')
    if password and not (8 <= length <= 63 or re.fullmatch(r'[0-9a-fA-F]{64}', password)):
        raise ValueError('Password must be empty for open Wi-Fi, 8..63 bytes, or a 64-digit hexadecimal key.')
    if not ssid and password:
        raise ValueError('Hotspot-only configuration must have an empty station password.')
    # JSON string escaping is compatible with C++ literals for these UTF-8 inputs.
    content = '#pragma once\n#define WIFI_SSID '+json.dumps(ssid,ensure_ascii=False)+'\n#define WIFI_PASSWORD '+json.dumps(password,ensure_ascii=False)+'\n'
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix='.wifi-', dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as out:
            out.write(content)
        os.replace(name, path)
    finally:
        if os.path.exists(name): os.unlink(name)


if __name__ == '__main__':
    try:
        values = sys.stdin.buffer.read().split(b'\0')
        if len(values) != 3 or values[-1] != b'':
            raise ValueError('Expected two NUL-separated values on stdin.')
        write_config(sys.argv[1], *(v.decode('utf-8') for v in values[:2]))
    except (ValueError, OSError, IndexError) as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(2)
