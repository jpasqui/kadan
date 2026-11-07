"""Fixed declaration repairs that retain the original reflected Script identities.

This source adapter changes a colliding C++ symbol, never the original type path.
Stock UHT's OverrideNativeName controls the registered reflected name. The loaded
path, property profile, and function signature still require the desktop gate.
"""
from pathlib import Path
import sys

STUBS = Path(__file__).resolve().parent
if str(STUBS) not in sys.path:
    sys.path.insert(0, str(STUBS))
import gen_headers

SUN_PATH = '/Script/Carrot4.SunPositionData'
SUN_CPP = 'FP4CarrotSunPositionData'
ENGINE_IDENTITIES = (
    '/Script/Engine.C4TerrainTag',
    '/Script/Engine.C4TerrainTagContainer',
    '/Script/Engine.EC4ExpontentialHeightFogBlend',
    '/Script/Engine.EC4Primitive',
    '/Script/Engine.EC4Stencil',
    '/Script/Engine.ParticleLightManager',
)


class Generator(gen_headers.Generator):
    def __init__(self, *args, native_engine_evidence=None, original_source_sha256=None, **kwargs):
        self.native_engine_evidence = native_engine_evidence
        self.original_source_sha256 = original_source_sha256
        self.native_engine = None
        super().__init__(*args, **kwargs)

    def files(self):
        if self.native_engine_evidence is not None:
            import fixed_engine_declarations
            rich = dict(source_sha256=self.original_source_sha256,
                        structures=list(self.db.structures.values()),
                        enums=list(self.db.enums.values()), functions=list(self.db.functions.values()))
            self.native_engine = fixed_engine_declarations.emit(
                rich, self.native_engine_evidence,
                {name: self.enum_form('/Script/Engine.' + name) for name in fixed_engine_declarations.ENUMS})
            if not self.native_engine['files']:
                raise ValueError('; '.join(self.native_engine['refusals']))
        files = super().files()
        if self.native_engine:
            files.update(self.native_engine['files'])
        return files

    def editor_path(self, path):
        if self.native_engine and path in self.native_engine['types']:
            return path
        return super().editor_path(path)

    def type_of(self, detail, context):
        # module_text removes manually registered terrain structs from the
        # automatic UHT declarations. Their independently checked storage still
        # exists in P4FixedEngineDeclarations and must remain usable by fields.
        if detail.get('kind') == 'StructProperty' and self.native_engine:
            path = detail.get('struct_path')
            evidence = self.native_engine.get('terrain_evidence')
            if path in ENGINE_IDENTITIES[:2] and path in self.native_engine['types'] and \
                    evidence and path in evidence.get('facts', {}):
                return self.cpp_name(path), True
        return super().type_of(detail, context)

    def _check_names(self):
        selected_sun = SUN_PATH in self.structs
        super()._check_names()
        if selected_sun and SUN_PATH not in self.structs:
            raise ValueError('exact SunPositionData C++ override collides with an installed type')

    def cpp_name(self, path):
        if path == SUN_PATH and path in self.db.structures:
            if self.db.structures[path]['kind'] != 'ScriptStruct':
                raise ValueError('original SunPositionData ScriptStruct required')
            return SUN_CPP
        return super().cpp_name(path)

    def module_text(self, module, uses):
        particle = '/Script/Engine.ParticleLightManager'
        manual_particle = self.native_engine and particle in self.native_engine['types'] and module == 'Carrot4'
        manual_terrain = set(ENGINE_IDENTITIES[:2]) & set(self.native_engine['types']) if self.native_engine and module == 'Carrot4' else set()
        if manual_particle:
            if particle not in self.classes:
                raise ValueError('required original ParticleLightManager selection absent')
            self.classes.remove(particle)
        if manual_terrain:
            if not manual_terrain.issubset(self.structs):
                raise ValueError('required original terrain selection absent')
            self.structs.difference_update(manual_terrain)
        try:
            enums, structs, classes, source = super().module_text(module, uses)
        finally:
            if manual_particle:
                self.classes.add(particle)
            self.structs.update(manual_terrain)
        if manual_terrain:
            before = '#include "Carrot4StubStructs.generated.h"'
            if structs.count(before) != 1:
                raise ValueError('manual Engine struct include boundary absent')
            structs = structs.replace(before, '#include "P4FixedEngineDeclarations.h"\n' + before, 1)
        if manual_particle:
            before = '#include "Carrot4StubClasses.generated.h"'
            if classes.count(before) != 1:
                raise ValueError('manual Engine class include boundary absent')
            classes = classes.replace(before, '#include "P4FixedEngineDeclarations.h"\n' + before, 1)
        if module == 'Carrot4' and SUN_PATH in self.structs:
            before = 'USTRUCT(BlueprintType)\nstruct ' + SUN_CPP + '\n'
            after = 'USTRUCT(BlueprintType, meta=(OverrideNativeName="SunPositionData"))\nstruct ' + SUN_CPP + '\n'
            if structs.count(before) != 1:
                raise ValueError('exact SunPositionData declaration repair boundary absent')
            structs = structs.replace(before, after, 1)
        return enums, structs, classes, source

    def identity_refusals(self):
        """Refuse source identity gaps; independent runtime gates remain required."""
        emitted = set(self.native_engine['types']) if self.native_engine else set()
        result = ['original Engine identity registration unqualified: ' + path
                  for path in ENGINE_IDENTITIES if path in self.relocated and path not in emitted]
        if self.native_engine:
            result += self.native_engine['refusals']
        return result
