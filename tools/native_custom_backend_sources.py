"""Stage the repository's first-party Maxwell compiler for a C++17 Unreal library build.

Original compiler files stay untouched. Staged adaptations replace standard bit-operation
spellings, avoid Unreal's OUT macro and make boolean accumulation explicit for MSVC.
There is no game code, shader corpus, third-party implementation or local path in them.
"""
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMPILER = ROOT / 'compiler'
SOURCES = ('nvasm_parse.cpp', 'nvasm_lower.cpp', 'nvasm_maxwell.cpp', 'maxwell_opt.cpp', 'maxwell_ra.cpp', 'maxwell_encode.cpp', 'maxwell_write.cpp',
           'ampere_write.cpp')
HEADERS = ('nvasm.h', 'nvasm_ir.h', 'nvasm_lower.h', 'nvasm_program.h', 'maxwell_mir.h', 'spirv_front.h', 'ampere_encode.h',
           'ampere_write.h')


def source_plan(root=ROOT, contents=None):
    """Return the fixed, adapted compiler bytes without touching a destination."""
    root = Path(root)
    wanted = {}
    def read(relative):
        return contents[relative] if contents is not None else (root / relative).read_bytes()
    provenance = dict(format=1, compiler='repository-first-party-nvasm-Maxwell', language='C++17', files={})
    for name in HEADERS + SOURCES:
        original = read('compiler/' + name)
        text = original.decode('utf-8')
        text = text.replace('#include <bit>', '#include "native_custom_backend_bits.h"')
        text = text.replace('std::popcount(', 'nvasm::portable::popcount(').replace('std::countr_zero(', 'nvasm::portable::countr_zero(')
        # Windows/Unreal defines OUT as an empty annotation macro. Preserve the
        # original compiler sources and rename only this staged MIR enum token.
        text = re.sub(r'\bOUT\b', 'NvOut', text)
        text = text.replace('used |= has(live, size_t(i.d + j));', 'used = (has(live, size_t(i.d + j)) != 0) || used;')
        text = text.replace('used |= has(live, pbit(i.pd));', 'used = (has(live, pbit(i.pd)) != 0) || used;')
        content = text.encode('utf-8')
        wanted['KadanMaxwell/' + name] = content
        provenance['files'][name] = dict(source='compiler/' + name,
            source_sha256=hashlib.sha256(original).hexdigest(), staged_sha256=hashlib.sha256(content).hexdigest())
    helper = root / 'core/native_custom_backend_bits.h'
    wanted['KadanMaxwell/' + helper.name] = read('core/' + helper.name)
    wanted['KadanMaxwell/SOURCE_PROVENANCE.json'] = (json.dumps(provenance, indent=2, sort_keys=True) + '\n').encode('utf-8')
    return wanted


def stage_core(target):
    """Offline research staging. Product staging validates ownership before writing."""
    target = Path(target)
    for name, content in source_plan().items():
        path = target / name
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists() or path.read_bytes() != content:
            path.write_bytes(content)
    return [target / 'KadanMaxwell' / name for name in SOURCES]


def build_fixture(output, compiler):
    """Offline incremental fixture build; never invokes Unreal or stages a product executable."""
    output = Path(output)
    sources = stage_core(output/'core')
    common = ['-std=c++17', '-O1', '-I'+str(ROOT/'core'), '-I'+str(output/'core')]
    compiler_headers = list((output/'core/KadanMaxwell').glob('*.h'))
    core_headers = list((ROOT/'core').rglob('*.h'))
    units = sources + [ROOT/'core/native_custom_backend.cpp',
                       ROOT/'core/native_custom_backend_policy.cpp',
                       ROOT/'tests/native_custom_backend_cli.cpp']
    objects=[]
    for unit in units:
        headers = compiler_headers if unit in sources else compiler_headers+core_headers
        stamp=hashlib.sha256(str(compiler).encode('utf-8')+unit.read_bytes()+b''.join(p.read_bytes() for p in sorted(headers))).hexdigest()
        obj=output/(unit.stem+'.obj'); signature=output/(unit.stem+'.build-sha256')
        if not obj.exists() or not signature.exists() or signature.read_text(encoding='utf-8')!=stamp:
            result=subprocess.run([str(compiler),*common,'-c',str(unit),'-o',str(obj)],capture_output=True,text=True,timeout=180)
            if result.returncode:raise RuntimeError(result.stderr)
            signature.write_text(stamp,encoding='utf-8')
        objects.append(obj)
    executable=output/'native_custom_backend_cli.exe'
    if not executable.exists() or any(p.stat().st_mtime>executable.stat().st_mtime for p in objects):
        result=subprocess.run([str(compiler),*map(str,objects),'-o',str(executable)],capture_output=True,text=True,timeout=60)
        if result.returncode:raise RuntimeError(result.stderr)
    return executable
