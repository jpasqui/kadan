"""Check compiler/ampere_encode.h's test cases with nvdisasm and pin their words in tests/ampere_encode_test.cpp.

    py -3.11 tests/ampere_encode_pin.py <ampere_encode_dump output prefix> <nvdisasm results>

The nvdisasm results are tab-separated lines "<hex>\t<text>" for each 16-byte instruction of <prefix>.bin, as the
desktop script dis.ps1 writes them (nvdisasm -b SM86 on each instruction alone). Every case's text must match
exactly; then the test's kPinned table is rewritten.
"""
import re
import sys
from pathlib import Path

prefix, results = sys.argv[1], sys.argv[2]
cases = [line.rstrip('\n').split('\t') for line in open(prefix + '.txt', encoding='utf-8')]
seen = [line.rstrip('\r\n').split('\t')[1].strip() for line in open(results, encoding='utf-8-sig')]
bad = [(c[2], s) for c, s in zip(cases, seen) if c[2].strip() != s]
if len(cases) != len(seen) or bad:
    print('nvdisasm disagrees:', bad[:10], len(cases), len(seen))
    sys.exit(1)
test = Path(__file__).with_name('ampere_encode_test.cpp')
text = test.read_text(encoding='utf-8')
rows = ',\n'.join('    {0x%s, 0x%s, 0x%s, 0x%s}' % tuple(c[1].split()) for c in cases)
text = re.sub(r'static const std::uint32_t kPinned\[\]\[4\] = \{.*?\n\};', 'static const std::uint32_t kPinned[][4] = {\n' + rows + ',\n};',
              text, flags=re.S)
test.write_text(text, encoding='utf-8', newline='\n')
print('%d cases match nvdisasm; pinned' % len(cases))
