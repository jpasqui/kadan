"""Write core/own_shader_payloads.h from the payloads in this folder.

    python own_shaders/make_payload_header.py [--check]

Kadan's plugin carries its own vertex and geometry programs inside its binary, so a creator's export needs no
file beside the plugin. The header holds each payload as base64 text in pieces short enough for the compiler's
string limits; own_export.h decodes them once. --check fails when the header differs from what this writes.
"""
import base64
import hashlib
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
HEADER = HERE.parent / 'core/own_shader_payloads.h'
PIECE = 4000


def text():
    lines = ['#pragma once',
             '// Kadan\'s own vertex and geometry programs (modding/mapkit/own_shaders), written by',
             '// modding/mapkit/own_shaders/make_payload_header.py. Do not edit: run the script.',
             '#include <cstddef>',
             'namespace P4MapKit { namespace CustomMaterial { namespace OwnShaders {',
             'struct Payload { const char* name; std::size_t size; const char* sha1; const char* const* pieces; std::size_t count; };']
    names = sorted(p.stem for p in HERE.glob('*.payload'))
    for index, name in enumerate(names):
        data = (HERE / (name + '.payload')).read_bytes()
        encoded = base64.b64encode(data).decode('ascii')
        pieces = [encoded[at:at + PIECE] for at in range(0, len(encoded), PIECE)]
        lines.append('inline const char* const kPieces{}[] = {{'.format(index))
        lines += ['    "{}",'.format(piece) for piece in pieces]
        lines.append('};')
    lines.append('inline const Payload kPayloads[] = {')
    for index, name in enumerate(names):
        data = (HERE / (name + '.payload')).read_bytes()
        lines.append('    {{"{}", {}, "{}", kPieces{}, sizeof(kPieces{}) / sizeof(kPieces{}[0])}},'.format(
            name, len(data), hashlib.sha1(data).hexdigest(), index, index, index))
    lines.append('};')
    lines.append('} } }')
    return '\n'.join(lines) + '\n'


def main():
    wanted = text()
    if '--check' in sys.argv[1:]:
        have = HEADER.read_text(encoding='utf-8') if HEADER.is_file() else ''
        if have != wanted:
            raise SystemExit('own_shader_payloads.h is not what make_payload_header.py writes')
        print('own_shader_payloads.h matches the payloads')
        return
    HEADER.write_text(wanted, encoding='utf-8', newline='\n')
    print('wrote', HEADER, len(wanted), 'bytes')


if __name__ == '__main__':
    main()
