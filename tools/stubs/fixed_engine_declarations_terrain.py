"""Source-bound declaration storage evidence; never imports original code bytes."""
import hashlib
import json
import struct
from pathlib import Path

RECEIPT_SHA256 = '5ea51527ed55d7259fe8bbb4d07484a83d60d4b92413580fe66fc671e84317dc'
SOURCE_SHA256 = 'fe616565ecfd35868fdd9a43525931a3bd46c383151e2a244cdf8e199a974190'
TAG = '/Script/Engine.C4TerrainTag'
CONTAINER = '/Script/Engine.C4TerrainTagContainer'
STORAGE = 'Source/Carrot4/Public/P4FixedTerrainStorage.h'


def verify_receipt(raw, source_sha256):
    """Verify the bounded exact independent receipt before admitting storage."""
    if not isinstance(raw, bytes) or not 0 < len(raw) <= 256 * 1024:
        raise ValueError('bounded exact terrain native receipt bytes required')
    digest = hashlib.sha256(raw).hexdigest()
    if digest != RECEIPT_SHA256:
        raise ValueError('independent terrain lifecycle receipt hash differs')
    data = json.loads(raw.decode('utf-8'))
    if data.get('format') != 'original-terrain-lifecycle/v1' or data.get('source_sha256') != source_sha256 or source_sha256 != SOURCE_SHA256:
        raise ValueError('original terrain executable binding differs')
    inner = data['inner_property']
    if [int(word, 16) for word in inner['words'][2:]] != [0x20000, 0x4500000019, 1, 0] or inner['name_utf8'] != 'TerrainTags':
        raise ValueError('original terrain inner property profile differs')
    params = data['particle_class_params']
    native = bytes.fromhex(params['raw_hex'])
    if len(native) != 96 or hashlib.sha256(native).hexdigest() != params['sha256']:
        raise ValueError('original native class parameter bytes differ')
    counts = struct.unpack_from('<5I', native, 56)
    config_address = int(params['address'], 16) + 8
    if counts != (2, 0, 0, 0, 0x1000a0) or struct.unpack_from('<Q', native, 8)[0] != 0 or any(int(address, 16) == config_address for address in params['relocations']):
        raise ValueError('original native class parameter/config profile differs')
    if [int(target, 16) for target in params['dependency_array']['targets']] != [0x39e7dd0, 0x5cacaf0] or params['cpp_class_info']['is_abstract'] != 0:
        raise ValueError('original native class dependencies/type info differ')
    facts = {}
    for name, path in (('tag', TAG), ('container', CONTAINER)):
        table = data['tables'][name]
        if len(table['targets']) != 39 or set(table['slot_code']) != {str(i) for i in range(39)}:
            raise ValueError('complete original terrain native ops evidence required')
        registration = data['registration_' + name]['words']
        if [int(registration[i], 16) for i in (4, 5, 8)] != [16, 8, 0x201] or int(registration[7], 16) >> 32 != 0x45:
            raise ValueError('original terrain registration profile differs')
        for slot in (2, 3, 7, 8, 9, 10, 11, 13, 14, 15, 16, 17, 18, 20,
                     23, 25, 27, 28, 29, 31, 32, 33, 34, 35, 38):
            if ' mov w0, wzr' not in table['slot_code'][str(slot)][0]:
                raise ValueError('original terrain unsupported native trait')
        for slot in (5, 21):
            if ' mov w0, #1' not in table['slot_code'][str(slot)][0]:
                raise ValueError('original terrain destructor/copy evidence differs')
        if ' mov x0, xzr' not in table['slot_code']['37'][0]:
            raise ValueError('original terrain computed property flags differ')
        facts[path] = dict(size=16, alignment=8, object_flags=0x45, uncomputed_struct_flags=0x201,
                           noop_constructor=False, zero_constructor=False, destructor=True,
                           plain_old_data=False, copy=True, computed_property_flags=0,
                           custom_serialization=False, custom_references=False)
    return dict(sha256=digest, source_sha256=source_sha256, facts=facts,
                particle_class_params=dict(class_flags=0x1000a0, config_name=None, dependencies=2,
                                           dependency_order=['CoreUObject.Object', '/Script/Engine'], is_abstract=False,
                                           functions=0, properties=0, interfaces=0, raw_sha256=params['sha256']))


def storage_text():
    # Only this first-party owned header is read; creator input never supplies CPP.
    return Path(__file__).with_name('fixed_engine_declarations_storage.h').read_text(encoding='utf-8')


def container_definition(api='CARROT4_API'):
    """Actual generated container; TArray lifecycle is supplied by stock UE."""
    if api not in ('CARROT4_API', ''):
        raise ValueError('fixed terrain CPP API token required')
    return '\n'.join([
        'struct ' + api + ' FC4TerrainTagContainer', '{',
        '    TArray<FC4TerrainTag> TerrainTags;',
        '    FC4TerrainTagContainer() = default;',
        '    FC4TerrainTagContainer(const FC4TerrainTagContainer& Other) : TerrainTags(Other.TerrainTags) {}',
        '    FC4TerrainTagContainer& operator=(const FC4TerrainTagContainer& Other)',
        '    {', '        TerrainTags = Other.TerrainTags;', '        return *this;', '    }',
        '    static UScriptStruct* StaticStruct();', '};', ''])


def emit(reflection, raw, source_sha256):
    evidence = verify_receipt(raw, source_sha256)
    rows = {row['path']: row for row in reflection['structures']}
    for path in (TAG, CONTAINER):
        row = rows.get(path)
        if not row or row['kind'] != 'ScriptStruct' or row.get('super_path') or row['observed_size'] != 16 or len(row['fields']) != 1:
            raise ValueError('original terrain own struct profile differs: ' + path)
    tag, array = rows[TAG]['fields'][0], rows[CONTAINER]['fields'][0]
    if (tag['name'], tag['offset'], tag['element_size'], tag['array_dim'], int(tag['flags'], 0), tag['type_detail']['kind']) != ('TerrainTag', 8, 4, 1, 0x48001040020201, 'UInt32Property'):
        raise ValueError('original terrain scalar profile differs')
    if (array['name'], array['offset'], array['element_size'], array['array_dim'], int(array['flags'], 0), array['type_detail']['kind']) != ('TerrainTags', 0, 16, 1, 0x40000000020201, 'ArrayProperty') or array['type_detail']['inner'].get('struct_path') != TAG:
        raise ValueError('original terrain array profile differs')
    header = [
        '#if !CPP', '// Native UHT mirrors; compiler storage below owns its vptr/lifecycle.',
        'USTRUCT(BlueprintType)', 'struct FC4TerrainTag', '{', '    GENERATED_BODY()',
        '    UPROPERTY(EditAnywhere, Category="Terrain") uint32 TerrainTag;', '};',
        'USTRUCT(BlueprintType)', 'struct FC4TerrainTagContainer', '{', '    GENERATED_BODY()',
        '    UPROPERTY(EditAnywhere, Category="Terrain") TArray<FC4TerrainTag> TerrainTags;', '};',
        '#else', container_definition(),
        'template<> struct TStructOpsTypeTraits<FC4TerrainTag> : TStructOpsTypeTraitsBase2<FC4TerrainTag>',
        '{ enum { WithCopy = true, WithZeroConstructor = false, WithNoInitConstructor = false, WithNoDestructor = false }; };',
        'template<> struct TStructOpsTypeTraits<FC4TerrainTagContainer> : TStructOpsTypeTraitsBase2<FC4TerrainTagContainer>',
        '{ enum { WithCopy = true, WithZeroConstructor = false, WithNoInitConstructor = false, WithNoDestructor = false }; };',
        'CARROT4_API UScriptStruct* Z_Construct_UScriptStruct_FC4TerrainTag();',
        'CARROT4_API UScriptStruct* Z_Construct_UScriptStruct_FC4TerrainTagContainer();', '#endif', '']
    cpp = ['#if P4_FIXED_ENGINE_NATIVE_ENUMS']
    for name in ('C4TerrainTag', 'C4TerrainTagContainer'):
        cpp_name = 'F' + name
        singleton = 'Z_Construct_UScriptStruct_' + cpp_name
        digest = int(hashlib.sha256((name + evidence['sha256']).encode('utf-8')).hexdigest()[:8], 16)
        cpp += ['static_assert(sizeof(' + cpp_name + ') == 16, "Original terrain storage differs");',
                'static_assert(alignof(' + cpp_name + ') == 8, "Original terrain alignment differs");',
                'static void* P4NewOps_' + name + '()', '{', '\t' + cpp_name + ' Probe;']
        member = 'TerrainTag' if name == 'C4TerrainTag' else 'TerrainTags'
        offset = 8 if name == 'C4TerrainTag' else 0
        cpp += ['\tcheckf(reinterpret_cast<const uint8*>(&Probe.' + member + ') - reinterpret_cast<const uint8*>(&Probe) == ' + str(offset) + ', TEXT("Original terrain member offset differs"));',
                '\treturn new UScriptStruct::TCppStructOps<' + cpp_name + '>();', '}',
                'CARROT4_API UScriptStruct* ' + singleton + '()', '{', '\tstatic UScriptStruct* Value = nullptr;',
                '\tif (!Value)', '\t{', '\t\tP4OriginalEnginePreflight();']
        if name == 'C4TerrainTag':
            cpp += ['\t\tstatic const UE4CodeGen_Private::FUInt32PropertyParams Scalar = {',
                    '\t\t\t"TerrainTag", nullptr, EPropertyFlags(0x48001040020001ull), UE4CodeGen_Private::EPropertyGenFlags::UInt32,',
                    '\t\t\tRF_Public|RF_Transient|RF_MarkAsNative, 1, 8, METADATA_PARAMS(nullptr, 0)', '\t\t};',
                    '\t\tstatic const UE4CodeGen_Private::FPropertyParamsBase* Properties[] = { reinterpret_cast<const UE4CodeGen_Private::FPropertyParamsBase*>(&Scalar) };']
        else:
            cpp += ['\t\tstatic const UE4CodeGen_Private::FStructPropertyParams Inner = {',
                    '\t\t\t"TerrainTags", nullptr, EPropertyFlags(0x20000ull), UE4CodeGen_Private::EPropertyGenFlags::Struct,',
                    '\t\t\tRF_Public|RF_Transient|RF_MarkAsNative, 1, 0, Z_Construct_UScriptStruct_FC4TerrainTag, METADATA_PARAMS(nullptr, 0)', '\t\t};',
                    '\t\tstatic const UE4CodeGen_Private::FArrayPropertyParams Array = {',
                    '\t\t\t"TerrainTags", nullptr, EPropertyFlags(0x40000000020001ull), UE4CodeGen_Private::EPropertyGenFlags::Array,',
                    '\t\t\tRF_Public|RF_Transient|RF_MarkAsNative, 1, 0, EArrayPropertyFlags::None, METADATA_PARAMS(nullptr, 0)', '\t\t};',
                    '\t\tstatic const UE4CodeGen_Private::FPropertyParamsBase* Properties[] = {',
                    '\t\t\treinterpret_cast<const UE4CodeGen_Private::FPropertyParamsBase*>(&Inner), reinterpret_cast<const UE4CodeGen_Private::FPropertyParamsBase*>(&Array)', '\t\t};']
        cpp += ['\t\tstatic const UE4CodeGen_Private::FStructParams Params = {',
                '\t\t\tP4OriginalEnginePackage, nullptr, P4NewOps_' + name + ', "' + name + '", 16, 8,',
                '\t\t\tProperties, UE_ARRAY_COUNT(Properties), RF_Public|RF_Transient|RF_MarkAsNative, 0x201u, METADATA_PARAMS(nullptr, 0)', '\t\t};',
                '\t\tUE4CodeGen_Private::ConstructUScriptStruct(Value, Params);', '\t}', '\treturn Value;', '}',
                'uint32 Get_' + singleton + '_Hash() { return ' + str(digest) + 'U; }',
                'UScriptStruct* ' + cpp_name + '::StaticStruct()', '{',
                '\tstatic UScriptStruct* Value = nullptr;',
                '\tif (!Value) Value = GetStaticStruct(' + singleton + ', P4OriginalEnginePackage(), TEXT("' + name + '"), sizeof(' + cpp_name + '), Get_' + singleton + '_Hash());',
                '\treturn Value;', '}',
                'template<> CARROT4_API UScriptStruct* StaticStruct<' + cpp_name + '>() { return ' + cpp_name + '::StaticStruct(); }',
                'static FCompiledInDeferStruct P4Defer_' + name + '(' + cpp_name + '::StaticStruct, TEXT("/Script/Engine"), TEXT("' + name + '"), false, nullptr, nullptr);', '']
    cpp += ['#endif', '']
    return dict(header=header, cpp=cpp, files={STORAGE: storage_text()}, types=[TAG, CONTAINER], evidence=evidence)
