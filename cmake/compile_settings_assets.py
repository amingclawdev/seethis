#!/usr/bin/env python3
"""Compile two known settings assets. Build-time metadata only, bounded nonblocking input."""
import argparse
import os
import pathlib
import stat

MAXIMUM_BYTES = 256 * 1024

def bounded_regular_bytes(path):
    # NONBLOCK prevents a substituted FIFO from blocking between checking/opening.
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    try:
        metadata = os.fstat(fd)
        if not stat.S_ISREG(metadata.st_mode):
            raise ValueError('Settings input must be a regular file: ' + str(path))
        if metadata.st_size > MAXIMUM_BYTES:
            raise ValueError('Settings input exceeds 256 KiB: ' + str(path))
        value = bytearray()
        while len(value) <= MAXIMUM_BYTES:
            block = os.read(fd, min(65536, MAXIMUM_BYTES + 1 - len(value)))
            if not block: break
            value.extend(block)
        if len(value) > MAXIMUM_BYTES:
            raise ValueError('Settings input grew beyond 256 KiB: ' + str(path))
        return bytes(value)
    finally:
        os.close(fd)

def generate(html, javascript, output):
    assets = [('IndexHTML', bounded_regular_bytes(html)), ('SettingsJS', bounded_regular_bytes(javascript))]
    lines = ['#pragma once', '#include <cstddef>', 'namespace seethis::service::settings_assets {']
    for name, value in assets:
        lines.append('inline constexpr unsigned char k' + name + 'Bytes[] = {')
        for offset in range(0, len(value), 32):
            lines.append('  ' + ','.join(str(x) for x in value[offset:offset + 32]) + ',')
        # Sentinel allows an empty fixed asset without zero-sized array extensions.
        lines += ['  0};', 'inline constexpr std::size_t k' + name + 'Length = ' + str(len(value)) + ';']
    lines.append('}')
    content = '\n'.join(lines) + '\n'
    output = pathlib.Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != content:
        output.write_text(content)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['html', 'javascript', 'output']: p.add_argument('--' + name, required=True)
    args = p.parse_args()
    generate(args.html, args.javascript, args.output)

if __name__ == '__main__': main()
