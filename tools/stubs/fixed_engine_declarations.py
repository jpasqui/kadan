"""First-party fixed Engine native registration source, never original game code.

UHT parses the normal enum declarations. A separate bounded generated-source
adapter removes their automatic singleton implementations before compilation.
The replacements retain those C++ symbols and register only original Engine
objects. Terrain native storage is refused until independently qualified.
"""
import hashlib
import re
from pathlib import Path

SOURCE = 'Source/Carrot4/'
MODE = SOURCE + 'Public/P4FixedEngineRegistrationMode.h'
CPP = SOURCE + 'Private/P4FixedEngineDeclarations.cpp'
HEADER = SOURCE + 'Public/P4FixedEngineDeclarations.h'
PARTICLE = '/Script/Engine.ParticleLightManager'
TERRAIN = ('/Script/Engine.C4TerrainTag', '/Script/Engine.C4TerrainTagContainer')
ENUMS = {
    'EC4ExpontentialHeightFogBlend': (('AlphaBlend', 0), ('Add', 1), ('EC4ExpontentialHeightFogBlend_MAX', 2)),
    'EC4Primitive': (('EC4P_Default', 0), ('EC4P_1', 1), ('EC4P_MAX', 2)),
    'EC4Stencil': (('EC4S_Default', 0), ('EC4S_1', 1), ('EC4S_2', 2), ('EC4S_3', 3), ('EC4S_MAX', 4)),
}
# UE4.26 ObjectMacros.h EClassFlags: Parsed180, TokenStreamAssembled224,
# Constructed238. These are state bits, not the CLASS_Inherit mask.
CLASS_STATE_BITS = {'CLASS_Constructed': 0x20000000,
                    'CLASS_TokenStreamAssembled': 0x00400000, 'CLASS_Parsed': 0x10}


def mode_text(enabled):
    return '#pragma once\n#define P4_FIXED_ENGINE_NATIVE_ENUMS ' + ('1' if enabled else '0') + '\n'


def registration_mode(text):
    """Accept only the owned body and the exact qualified source-label prefix."""
    prefix = '\ufeff' if text.startswith('\ufeff') else ''
    body = text[len(prefix):]
    label = 'P4Stubs/' + MODE
    labels = ('#line 1 "' + label + '"\n',
              '#ifdef __cplusplus\n#line 0 "' + label + '"\n#endif\n')
    for qualified in labels:
        if body.startswith(qualified):
            prefix += qualified
            body = body[len(qualified):]
            break
    if body not in (mode_text(False), mode_text(True)):
        raise ValueError('fixed native registration mode header changed')
    return prefix, body == mode_text(True)


def set_manual_registration_mode(plugin, enabled):
    """Change only the owned mode header, before UHT/action graph generation."""
    from fixed_engine_declarations_uht import canonical
    path = canonical(Path(plugin) / MODE)
    if not path.is_file():
        raise ValueError('fixed native registration mode header absent')
    prior = path.read_bytes().decode('utf-8')
    prefix, unused = registration_mode(prior)
    text = prefix + mode_text(bool(enabled))
    if text != prior:
        path.write_text(text, encoding='utf-8', newline='\n')
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def prepare_generated_registration(host_plugin, actions_path):
    from fixed_engine_declarations_uht import prepare_generated_registration as prepare
    return prepare(host_plugin, actions_path)


def validate_generated_registration(receipt):
    from fixed_engine_declarations_uht import validate_generated_registration as validate
    return validate(receipt)


def emit(reflection, native_evidence, enum_forms):
    """Return source probes plus named refusals; source emission is not admission.

    native_evidence binds independently observed class flags/cast flags/size and
    alignment to reflection's original executable. Missing evidence refuses the
    class. Each enum form must be the UHT-visible CPP declaration's exact form.
    """
    refusals = ['original native storage/lifecycle unqualified: ' + p for p in TERRAIN]
    source_hash = reflection.get('source_sha256', '')
    if not re.fullmatch('[0-9a-f]{64}', source_hash) or native_evidence.get('source_sha256') != source_hash:
        return dict(files={}, refusals=refusals + ['original executable evidence binding absent'], types=[])
    enum_rows = {r['path']: r for r in reflection['enums']}
    pieces = ['// First-party declaration-only native registration; no game behavior.',
              '#include "P4FixedEngineDeclarations.h"', '#include "P4FixedEngineRegistrationMode.h"',
              '#include "Carrot4StubEnums.h"', '#include "UObject/Package.h"',
              '#include "UObject/UObjectGlobals.h"', '',
              'static UObject* P4OriginalEnginePackage()', '{',
              '\treturn FindObjectChecked<UPackage>(nullptr, TEXT("/Script/Engine"));', '}', '',
              '#if P4_FIXED_ENGINE_NATIVE_ENUMS']
    admitted = []
    for name, expected in ENUMS.items():
        path = '/Script/Engine.' + name
        row = enum_rows.get(path)
        values = tuple((v['name'], v['value']) for v in row.get('values', ())) if row else ()
        exact = tuple((name + '::' + n, v) for n, v in expected)
        if values != exact:
            refusals.append('original enum names/values differ: ' + path)
            continue
        form = enum_forms.get(name)
        if form not in ('class', 'namespace', 'plain'):
            refusals.append('UHT enum CPP form absent: ' + path)
            continue
        cpp_type = name + '::Type' if form == 'namespace' else name
        cpp_form = {'class': 'EnumClass', 'namespace': 'Namespaced', 'plain': 'Regular'}[form]
        singleton = 'Z_Construct_UEnum_Carrot4_' + name
        digest = int(hashlib.sha256(repr((path, expected, form)).encode('utf-8')).hexdigest()[:8], 16)
        pieces += ['CARROT4_API UEnum* ' + singleton + '()', '{',
                   '\tstatic UEnum* Value = nullptr;', '\tif (!Value)', '\t{',
                   '\t\tP4OriginalEnginePreflight();',
                   '\t\tcheckf(!FindObject<UObject>(P4OriginalEnginePackage(), TEXT("' + name + '")), TEXT("Original Engine enum identity collision"));',
                   '\t\tstatic const UE4CodeGen_Private::FEnumeratorParam Entries[] = {']
        pieces += ['\t\t\t{"' + n + '", ' + str(v) + '},' for n, v in exact]
        pieces += ['\t\t};', '\t\tstatic const UE4CodeGen_Private::FEnumParams Params = {',
                   '\t\t\tP4OriginalEnginePackage, nullptr, "' + name + '", "' + cpp_type + '",',
                   '\t\t\tEntries, UE_ARRAY_COUNT(Entries), RF_Public|RF_Transient|RF_MarkAsNative, EEnumFlags::None,',
                   '\t\t\t// Dynamic selects bAddMax=false only; deferred registration below is native.',
                   '\t\t\tUE4CodeGen_Private::EDynamicType::Dynamic, uint8(UEnum::ECppForm::' + cpp_form + '),',
                   '\t\t\tMETADATA_PARAMS(nullptr, 0)', '\t\t};',
                   '\t\tUE4CodeGen_Private::ConstructUEnum(Value, Params);', '\t}', '\treturn Value;', '}',
                   'static UEnum* P4StaticEnum_' + name + '()', '{',
                   '\tstatic UEnum* Value = nullptr;', '\tif (!Value) Value = GetStaticEnum(' + singleton + ', P4OriginalEnginePackage(), TEXT("' + name + '"));',
                   '\treturn Value;', '}',
                   'template<> CARROT4_API UEnum* StaticEnum<' + cpp_type + '>()', '{',
                   '\treturn P4StaticEnum_' + name + '();', '}',
                   'uint32 Get_' + singleton + '_Hash() { return ' + str(digest) + 'U; }',
                   'static FCompiledInDeferEnum P4Defer_' + name + '(P4StaticEnum_' + name + ', TEXT("/Script/Engine"), TEXT("' + name + '"), false, nullptr, nullptr);', '']
        admitted.append(path)
    pieces += ['#endif', '']
    header = ['#pragma once', '#include "CoreMinimal.h"', '#include "UObject/Object.h"',
              '#include "UObject/Class.h"', '']
    class_flag_plan = None
    classes = {r['path']: r for r in reflection['structures']}
    row, evidence = classes.get(PARTICLE), native_evidence.get('classes', {}).get(PARTICLE)
    try:
        if not row or row['kind'] != 'Class' or row['super_path'] != '/Script/CoreUObject.Object' or row['fields']:
            raise ValueError('original parent/own field profile differs')
        if not evidence or evidence['status'] != 'observed':
            raise ValueError('original native class flags/alignment absent')
        flags, cast = int(evidence['class_flags'], 0), int(evidence['class_cast_flags'], 0)
        config = evidence['config_name']
        if not isinstance(config, str) or not re.fullmatch('[A-Za-z_][A-Za-z_0-9]*', config):
            raise ValueError('original native class config name absent/unsupported')
        size, alignment = evidence['observed_size'], evidence['min_alignment']
        if size != row['observed_size'] or size != 40 or alignment != 8:
            raise ValueError('original empty UObject storage/alignment differs')
        if not flags & 0x80 or flags & (0x40000 | 0x4000 | 0x80000000) or not 0 <= cast < 2**64:
            raise ValueError('original native UObject class flags unsupported')
        if any(r['path'].startswith(PARTICLE + '.') for r in reflection.get('functions', ())):
            raise ValueError('original own function profile is nonempty')
        registration_flags = int(evidence['registration_flags'], 0)
        if not 0 <= registration_flags < 2**32:
            raise ValueError('original native class initial registration flags unsupported')
        class_params_flags = int(evidence['class_params_flags'], 0)
        class_params_config = evidence['class_params_config_name']
        if not 0 <= class_params_flags < 2**32 or class_params_flags & sum(CLASS_STATE_BITS.values()):
            raise ValueError('original native class parameter flags unsupported')
        if class_params_config is not None and (not isinstance(class_params_config, str) or not re.fullmatch('[A-Za-z_][A-Za-z_0-9]*', class_params_config)):
            raise ValueError('original native class parameter config name unsupported')
        config_cpp = 'nullptr' if class_params_config is None else '"' + class_params_config + '"'
        class_params_receipt = None
        if 'terrain_receipt_bytes' in native_evidence:
            import fixed_engine_declarations_terrain as terrain
            proof = terrain.verify_receipt(native_evidence['terrain_receipt_bytes'], source_hash)
            if (class_params_flags, class_params_config) != (proof['particle_class_params']['class_flags'], proof['particle_class_params']['config_name']):
                raise ValueError('original raw native class parameters differ from explicit evidence')
            class_params_receipt = proof['sha256']
        class_flag_plan = dict(observed_final=hex(flags), observed_initial=hex(registration_flags),
                               observed_class_params=hex(class_params_flags), class_params_config_name=class_params_config,
                               stock_state_bits={name: hex(value) for name, value in CLASS_STATE_BITS.items()},
                               stock_definition='UE4.26 ObjectMacros.h EClassFlags',
                               original_class_params_status='source-bound-receipt' if class_params_receipt else 'explicit-observed-input',
                               original_class_params_receipt_sha256=class_params_receipt,
                               dependency_order=['CoreUObject.Object', '/Script/Engine'], is_abstract=False,
                               runtime_qualified=False)
        header += ['#if !CPP', '// UHT-only intrinsic mirror emits no class singleton or boilerplate.',
                   'UCLASS(intrinsic, noexport)', 'class UParticleLightManager : public UObject', '{',
                   '\tGENERATED_BODY()', 'public:',
                   '\tUParticleLightManager(const FObjectInitializer& Init);', '};',
                   '#else', 'class CARROT4_API UParticleLightManager : public UObject', '{',
                   '\tstatic void StaticRegisterNativesUParticleLightManager();', 'public:',
                   '\tDECLARE_CLASS(UParticleLightManager, UObject, EClassFlags(0x' + format(registration_flags, 'x') + 'u), EClassCastFlags(0x' + format(cast, 'x') + 'ull), TEXT("/Script/Engine"), NO_API)',
                   '\tDECLARE_SERIALIZER(UParticleLightManager)',
                   '\tstatic const TCHAR* StaticConfigName() { return TEXT("' + config + '"); }',
                   '\tUParticleLightManager(const FObjectInitializer& Init = FObjectInitializer::Get()) : Super(Init) {}',
                   '\tUParticleLightManager(FVTableHelper& Helper) : Super(Helper) {}',
                   '\tDEFINE_DEFAULT_OBJECT_INITIALIZER_CONSTRUCTOR_CALL(UParticleLightManager)',
                   '\tstatic UObject* __VTableCtorCaller(FVTableHelper&) { return nullptr; }', '};',
                   'CARROT4_API UClass* Z_Construct_UClass_UParticleLightManager();',
                   'CARROT4_API UClass* Z_Construct_UClass_UParticleLightManager_NoRegister();',
                   '#endif', '']
        pieces += ['// The original class has no own native storage; use this editor\'s UObject ABI.',
                   'static_assert(sizeof(UParticleLightManager) == sizeof(UObject), "ParticleLightManager adds native storage");',
                   'static_assert(alignof(UParticleLightManager) == ' + str(alignment) + ', "Original ParticleLightManager alignment differs");',
                   'void UParticleLightManager::StaticRegisterNativesUParticleLightManager() {}',
                   'static TClassCompiledInDefer<UParticleLightManager> P4ParticleClassInfo(TEXT("UParticleLightManager"), sizeof(UParticleLightManager), 0);',
                   'UClass* UParticleLightManager::GetPrivateStaticClass()', '{',
                   '\tstatic UClass* Value = nullptr;', '\tif (!Value)', '\t{',
                   '\t\tP4OriginalEnginePreflight();',
                   '\t\tGetPrivateStaticClassBody(StaticPackage(), TEXT("ParticleLightManager"), Value, StaticRegisterNativesUParticleLightManager,',
                   '\t\t\tsizeof(UParticleLightManager), alignof(UParticleLightManager), EClassFlags(StaticClassFlags), StaticClassCastFlags(), StaticConfigName(),',
                   '\t\t\tInternalConstructor<UParticleLightManager>, InternalVTableHelperCtorCaller<UParticleLightManager>,',
                   '\t\t\t&UParticleLightManager::AddReferencedObjects, &Super::StaticClass, &WithinClass::StaticClass);',
                   '\t}', '\treturn Value;', '}',
                   'CARROT4_API UClass* Z_Construct_UClass_UParticleLightManager_NoRegister() { return UParticleLightManager::StaticClass(); }',
                   'CARROT4_API UClass* Z_Construct_UClass_UParticleLightManager()', '{',
                   '\tstatic UClass* Value = nullptr;', '\tif (!Value)', '\t{',
                   '\t\textern COREUOBJECT_API UClass* Z_Construct_UClass_UObject();',
                   '\t\tstatic UObject* (*const Dependencies[])() = {',
                   '\t\t\treinterpret_cast<UObject*(*)()>(Z_Construct_UClass_UObject), P4OriginalEnginePackage', '\t\t};',
                   '\t\tstatic const FCppClassTypeInfoStatic TypeInfo = {false};',
                   '\t\t// Original raw FClassParams differ from initial factory and final observed class flags.',
                   '\t\t// Final loaded equality remains an independent qualification gate.',
                   '\t\tstatic const UE4CodeGen_Private::FClassParams Params = {',
                   '\t\t\tUParticleLightManager::StaticClass, ' + config_cpp + ', &TypeInfo, Dependencies, nullptr, nullptr, nullptr,',
                   '\t\t\tUE_ARRAY_COUNT(Dependencies), 0, 0, 0, 0x' + format(class_params_flags, 'x') + 'u, METADATA_PARAMS(nullptr, 0)',
                   '\t\t};',
                   '\t\tUE4CodeGen_Private::ConstructUClass(Value, Params);',
                   '\t\tcheckf(Value->GetPathName() == TEXT("/Script/Engine.ParticleLightManager"), TEXT("Original Engine class identity differs"));',
                   '\t}', '\treturn Value;', '}',
                   'static FCompiledInDefer P4Defer_ParticleLightManager(Z_Construct_UClass_UParticleLightManager, UParticleLightManager::StaticClass, TEXT("/Script/Engine"), TEXT("UParticleLightManager"), false);', '']
        admitted.append(PARTICLE)
    except (KeyError, TypeError, ValueError) as error:
        refusals.append('original native class registration unqualified: ' + PARTICLE + ': ' + str(error))
    extra_files, terrain_evidence = {}, None
    if 'terrain_receipt_bytes' in native_evidence:
        try:
            import fixed_engine_declarations_terrain as terrain
            native = terrain.emit(reflection, native_evidence['terrain_receipt_bytes'], source_hash)
            header[4:4] = ['#if CPP', '#define P4_FIXED_TERRAIN_API CARROT4_API',
                          '#include "P4FixedTerrainStorage.h"', '#undef P4_FIXED_TERRAIN_API', '#endif',
                          '#include "P4FixedEngineDeclarations.generated.h"']
            header += native['header']
            pieces += native['cpp']
            extra_files.update(native['files'])
            terrain_evidence = native['evidence']
            admitted += native['types']
            refusals = [why for why in refusals if not any(why.endswith(path) for path in TERRAIN)]
        except (KeyError, TypeError, ValueError) as error:
            refusals.append('original terrain lifecycle evidence refused: ' + str(error))
    # All Engine identity absence guards run before any of these native objects
    # is created. UE's registration callbacks execute on its normal startup path.
    preflight = ['static void P4OriginalEnginePreflight()', '{',
                 '\tstatic bool Complete = false;', '\tif (Complete) return;',
                 '\tUObject* Outer = P4OriginalEnginePackage();']
    preflight += ['\tcheckf(!FindObject<UObject>(Outer, TEXT("' + path.rsplit('.', 1)[1] + '")), TEXT("Original Engine identity collision"));'
                  for path in sorted(admitted)]
    preflight += ['\tComplete = true;', '}', '']
    marker = pieces.index('#if P4_FIXED_ENGINE_NATIVE_ENUMS')
    pieces[marker:marker] = preflight
    # This is a source probe: the adapter and actual UHT/load/cook gates are separate.
    extra_files.update({MODE: mode_text(False), HEADER: '\n'.join(header), CPP: '\n'.join(pieces)})
    return dict(files=extra_files, refusals=refusals, types=admitted,
                terrain_evidence=terrain_evidence, class_flag_plan=class_flag_plan, runtime_qualified=False)
