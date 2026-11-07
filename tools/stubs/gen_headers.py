"""Stub C++ modules for the game's native classes, generated from the reflection capture.

    py -3.11 tools/stubs/gen_headers.py --coverage private/mapkit-step2/coverage-Cave000_F00.json
        --engine-index private/mapkit-step2/engine-index.json --subobjects private/mapkit-step2/subobjects.json
        --out private/mapkit-step2/plugin/P4Stubs [--closure full] [--expose all] [--defaults defaults.json]

Writes an Unreal 4.26 plugin with one module per game script package (Carrot4, AkAudio, ...), so the
classes keep their /Script/<Package>.<Name> paths. Each module has three headers (enums, structs,
classes), one .cpp (constructors that create the default subobjects) and a Build.cs. The classes have
the game's names, parents and properties, and no behavior.

Closure. "minimal" (default) writes the requested classes, their parents, and every struct and enum
they hold by value. An object reference to a class outside that set is declared with the nearest
parent that is inside it or in the engine. Tagged serialization stores object references without
their class, so this does not change what is cooked. "full" writes every class of the game modules.

Expose. "game" (default) gives each property the editor flags the game has. "all" also makes every
saved property editable, which the level importer needs to set values.

Defaults. With --defaults (eden_defaults.py's reading of the running game) every class gets the game's
default values and default subobjects: its constructor makes the subobjects the game's class default
object has and sets each value through the plugin's P4StubsCore module (core_module/). Without it a
stub's values are all zero, and a cooked level stores the difference from zero instead of from the
game's defaults. --subobjects is the older source for the subobjects (from cooked maps) and is not
used for a class the defaults cover.

Functions. With --functions every class and interface also declares the reflected functions the game's
class has (name, parameters, return type, and the Blueprint, static, const and event flags), with
bodies that do nothing. Blueprints recovered from the game call them by name, so their graphs need the
declarations. Default parameter values and other editor-only metadata are not in the game.

Engine additions. The game's engine adds properties to engine classes (PrimitiveComponent.C4Stencil,
...). The launcher's engine does not have them, so a value for one cannot be held. With
--engine-declared (what the editor's classes declare, written by tools host.py declared)
each game class directly under an engine class declares the added properties of its engine parents
itself. A property tag names a property without its class, so cooked data is the same. An engine
class used as it is (a plain StaticMeshComponent) still cannot hold them.
"""
import argparse
import collections
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import reflection  # noqa: E402
from reflection import OBJECT, in_engine, module_of  # noqa: E402

CORE = 'P4StubsCore'
# Set by the constructor's own lines (the root, SetupAttachment), not as values.
STRUCTURE = {'Actor.RootComponent', 'SceneComponent.AttachParent', 'SceneComponent.AttachChildren',
             'SceneComponent.AttachSocketName'}
ACTOR = '/Script/Engine.Actor'
COMPONENT = '/Script/Engine.ActorComponent'
INTERFACE = '/Script/CoreUObject.Interface'
PRIMITIVE = {'Int8Property': 'int8', 'Int16Property': 'int16', 'IntProperty': 'int32', 'Int64Property': 'int64',
             'UInt16Property': 'uint16', 'UInt32Property': 'uint32', 'UInt64Property': 'uint64',
             'FloatProperty': 'float', 'DoubleProperty': 'double', 'NameProperty': 'FName',
             'StrProperty': 'FString', 'TextProperty': 'FText'}
# Types UE 4.26 Blueprints cannot hold. UnrealHeaderTool rejects Blueprint specifiers on them.
NOT_BLUEPRINT = {'Int8Property', 'Int16Property', 'UInt16Property', 'UInt32Property', 'UInt64Property',
                 'DoubleProperty', 'FieldPathProperty', 'MulticastSparseDelegateProperty'}
VALUE_INIT = {'Int8Property', 'Int16Property', 'IntProperty', 'Int64Property', 'UInt16Property', 'UInt32Property',
              'UInt64Property', 'FloatProperty', 'DoubleProperty', 'BoolProperty', 'ByteProperty', 'EnumProperty',
              'ObjectProperty', 'ClassProperty'}
# Core math structs whose default constructor leaves the members unset. The editor warns at startup
# about a USTRUCT with such a member ("is not initialized properly").
FORCE_INIT = {'/Script/CoreUObject.' + n for n in ('Vector', 'Vector2D', 'Vector4', 'Rotator', 'Quat', 'LinearColor',
                                                   'Color', 'IntPoint', 'IntVector', 'Box', 'Box2D', 'Plane', 'Matrix')}
UNDERLYING = {'ByteProperty': 'uint8', 'Int8Property': 'int8', 'Int16Property': 'int16', 'IntProperty': 'int32',
              'Int64Property': 'int64', 'UInt16Property': 'uint16', 'UInt32Property': 'uint32',
              'UInt64Property': 'uint64'}
# Smallest first, unsigned before signed.
RANGE = {'uint8': (0, 2 ** 8 - 1), 'uint16': (0, 2 ** 16 - 1), 'uint32': (0, 2 ** 32 - 1), 'int8': (-2 ** 7, 2 ** 7 - 1),
         'int16': (-2 ** 15, 2 ** 15 - 1), 'int32': (-2 ** 31, 2 ** 31 - 1), 'int64': (-2 ** 63, 2 ** 63 - 1),
         'uint64': (0, 2 ** 64 - 1)}
TICK = '/Script/Engine.TickFunction'
# Engine classes with virtual functions the editor does not export. A class in another module links
# only if it has those functions itself, so the first stub class under one of these overrides them:
# with nothing for the editor-only ones, with the grandparent's for the others. Found by the linker.
UNEXPORTED_VIRTUALS = {
    '/Script/Engine.InterpTrack': [
        '\tvirtual const FString GetEdHelperClassName() const override { return FString(); }',
        '\tvirtual const FString GetSlateHelperClassName() const override { return FString(); }',
        '#if WITH_EDITORONLY_DATA',
        '\tvirtual class UTexture2D* GetTrackIcon() const override { return nullptr; }',
        '#endif', ''],
    '/Script/Engine.PostProcessComponent': [
        '\tvirtual void OnRegister() override { USceneComponent::OnRegister(); }',
        '\tvirtual void OnUnregister() override { USceneComponent::OnUnregister(); }',
        '\tvirtual void Serialize(FArchive& Ar) override { USceneComponent::Serialize(Ar); }', ''],
}
# UMG's abstract widget bases leave RebuildWidget to their subclasses and ensure when one is missing. The
# game's widgets directly under them (UI particles, Wwise pickers) have no Slate widget here, so their stubs
# build an empty one. A class under a concrete engine widget (Image, TextBlock) keeps that widget's.
ABSTRACT_WIDGET_PARENTS = {'/Script/UMG.Widget', '/Script/UMG.PanelWidget', '/Script/UMG.ContentWidget',
                           '/Script/UMG.TextLayoutWidget'}
EMPTY_WIDGET = ['protected:', '\tvirtual TSharedRef<SWidget> RebuildWidget() override { return SNew(SSpacer); }',
                'public:', '']
# Native serializers of game-plugin classes outside /Script/Carrot4 (no translated overrides). Each
# matches the editor declaration that cooked the content: a cooked package's native tail must be read
# by the same code that wrote it, and a cook must write what the game reads.
NATIVE_SERIALIZERS = {
    # Wwise integration: the cooked platform data ends with its current asset-data reference.
    '/Script/AkAudio.AkAssetPlatformData': [
        '\tvirtual void Serialize(FArchive& Ar) override { Super::Serialize(Ar); Ar << CurrentAssetData; }', ''],
    # Wwise integration: asset data ends with its bulk data, empty in every package of the game.
    '/Script/AkAudio.AkAssetData': [
        '\tFByteBulkData Data;',
        '\tvirtual void Serialize(FArchive& Ar) override { Super::Serialize(Ar); Data.Serialize(Ar, this); }', ''],
}
COUNT = ['', '_OneParam', '_TwoParams', '_ThreeParams', '_FourParams', '_FiveParams', '_SixParams',
         '_SevenParams', '_EightParams', '_NineParams']
# Enumerator names that Windows or the engine define as macros.
MACROS = {'TRUE', 'FALSE', 'IN', 'OUT', 'ERROR', 'NO_ERROR', 'DELETE', 'OPTIONAL', 'CONST', 'VOID', 'NEAR', 'FAR',
          'near', 'far', 'min', 'max', 'small', 'IGNORE', 'INFINITE', 'PASSTHROUGH', 'TRANSPARENT', 'OPAQUE',
          'DOMAIN', 'OVERFLOW', 'UNDERFLOW', 'RELATIVE', 'ABSOLUTE', 'interface', 'PI', 'NULL', 'EOF', 'check',
          'verify', 'TEXT', 'BYTE', 'BOOL', 'DWORD', 'INT', 'UINT', 'FLOAT', 'CALLBACK', 'STRICT', 'DIFFERENCE'}
KEYWORDS = set('''alignas alignof and asm auto bool break case catch char class const constexpr continue
decltype default delete do double else enum explicit export extern false float for friend goto if inline int
long mutable namespace new noexcept not nullptr operator or private protected public register return short
signed sizeof static struct switch template this throw true try typedef typeid typename union unsigned using
virtual void volatile while xor'''.split())
IDENT = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')

F_EDIT, F_BP, F_EXPORT, F_BP_RO, F_NET = 0x1, 0x4, 0x8, 0x10, 0x20
F_FIXED, F_NO_TEMPLATE, F_TRANSIENT, F_CONFIG = 0x40, 0x800, 0x2000, 0x4000
F_NO_INSTANCE, F_EDIT_CONST, F_INSTANCED_REF, F_DUP_TRANSIENT = 0x10000, 0x20000, 0x80000, 0x200000
F_SAVE_GAME, F_NO_CLEAR, F_ASSIGNABLE, F_DEPRECATED = 0x1000000, 0x2000000, 0x10000000, 0x20000000
F_INTERP, F_NON_TRANSACTIONAL, F_SEARCHABLE = 0x200000000, 0x400000000, 0x10000000000
F_SIMPLE, F_ADVANCED, F_CALLABLE, F_TEXT_TRANSIENT = 0x20000000000, 0x40000000000, 0x100000000000, 0x400000000000
F_NON_PIE_TRANSIENT, F_EXPOSE_ON_SPAWN, F_PERSISTENT = 0x800000000000, 0x1000000000000, 0x2000000000000
F_SKIP = 0x80000000000000
KEPT = 'P4Kept_'   # the editor's own reference to a default subobject the game holds only in a transient property
FUNC_EXEC, FUNC_NATIVE, FUNC_STATIC, FUNC_DELEGATE = 0x200, 0x400, 0x2000, 0x100000
FUNC_PROTECTED = 0x80000
FUNC_CALLABLE, FUNC_BP_EVENT, FUNC_PURE, FUNC_CONST = 0x4000000, 0x8000000, 0x10000000, 0x40000000
LATENT = '/Script/Engine.LatentActionInfo'
DELEGATES = ('DelegateProperty', 'MulticastInlineDelegateProperty', 'MulticastSparseDelegateProperty',
             'MulticastDelegateProperty')


class Generator:
    def __init__(self, db, engine_index=None, subobjects=None, expose='game', plugin='P4Stubs', defaults=None,
                 functions=False, engine_declared=None, zero_constructor=None, engine_defaults=(), runtime=None):
        self.db = db
        # The UE4 recomp bridge (decomp/ue4-recomp-bridge/class_plan.py): classes laid out at the
        # game's offsets, running the game's original code (carrot4_gen.py's overrides and
        # constructors). None: the ordinary stubs.
        self.runtime = runtime
        # [(class path, property)] of the engine's own classes whose default the game's engine has
        # otherwise: the value comes from the capture, and the stub module gives it to every object
        # of the class (core_module/Public/P4StubDefaults.h says how).
        self.engine_defaults = list(engine_defaults)
        # Structs the game's code makes by filling them with zeros (the engine's "zero constructor"
        # trait). A function with a local variable of one has no "has defaults" flag.
        self.zero_constructor = set(zero_constructor or ())
        # class or struct path -> lower-case names of the properties the launcher's editor declares
        self.engine_declared = {path: {name.lower() for name in record['p']}
                                for path, record in (engine_declared or {}).items()}
        self.functions_of = collections.defaultdict(list)  # class path -> names of its reflected functions
        if functions:
            for full, function in db.functions.items():
                owner, name = full.rsplit('.', 1)
                if not int(function['flags'], 16) & FUNC_DELEGATE:
                    self.functions_of[owner].append(name)
        self.function_stats = collections.Counter()
        self.defaults = (defaults or {}).get('classes', {})
        self.default_stats = collections.Counter()
        self.default_table = []  # [class path, subobject, property path, value, kind] for every row written
        self.default_config = {}  # class path -> {property: text}: values the editor reads from the project's ini
        self.default_config_elements = {}  # (class path, property) -> [text of each element] for arrays
        self.index = engine_index or dict(classes={}, structs={}, enums={}, delegates={})
        self.subobjects = subobjects or {}
        self.expose = expose
        self.plugin = plugin
        self.classes, self.structs, self.enums = set(), set(), set()
        self.enum_use = collections.defaultdict(set)  # enum path -> {'byte', 'enum'}
        self.enum_width = {}
        self.delegates = {}  # signature path -> (cpp name, declaration or None, header it is declared in)
        self.phase = 'Structs'
        self.hashed = set()  # generated structs used as set elements or map keys
        # Engine-package structs and enums that the launcher engine does not have (the game's engine
        # fork added them) -> the stub module that declares them instead. A property tag stores the
        # struct or enum name without its package, so the cooked data is the same.
        self.relocated = {}
        self.notes = []

    # ---- selection -------------------------------------------------------------------------

    def external(self, path):
        """True for a type the editor provides."""
        return in_engine(path) and path not in self.relocated

    def home(self, path):
        """The module whose headers declare a type."""
        return self.relocated.get(path) or module_of(path)

    def fork_type(self, kind, path, user):
        """Move an engine-package type the launcher engine lacks into the module of its user."""
        if path in self.relocated:
            return True
        if not in_engine(path) or module_of(path) == 'CoreUObject' or not self.index[kind]:
            return False
        if module_of(path) + '.' + path.rsplit('.', 1)[1] in self.index[kind]:
            return False
        if path not in (self.db.structures if kind == 'structs' else self.db.enums):
            return False
        self.relocated[path] = self.home(user) if not in_engine(user) or user in self.relocated else sorted(reflection.GAME_MODULES)[0]
        self.note(f'{path} is not in the launcher engine: declared in module {self.relocated[path]} instead')
        return True

    def fork_class(self, path, user):
        """Move an engine-package class the launcher engine lacks, and a game class derives from, into
        the module of that game class. A cooked level names an actor's own class, not its parents."""
        if path in self.relocated:
            return True
        if not in_engine(path) or module_of(path) == 'CoreUObject' or not self.index['classes'] or \
                module_of(path) + '.' + path.rsplit('.', 1)[1] in self.index['classes']:
            return False
        self.relocated[path] = self.home(user)
        self.note(f'{path} is not in the launcher engine: declared in module {self.relocated[path]} instead')
        return True

    def fork_fields(self, path, owners=False):
        """Properties the game's engine adds to the engine parents of a game class that derives
        from an engine class directly: this class declares them, the launcher's engine cannot.
        owners: return (field, engine class that has it in the game) pairs."""
        record = self.db.structures.get(path)
        if not self.engine_declared or record is None or record['kind'] != 'Class' or path in self.relocated:
            return []
        parent = record['super_path']
        if not parent or not self.external(parent):
            return []
        taken = {field['name'].lower() for field in record['fields']}
        out = []
        for ancestor in self.db.chain(parent):
            known = self.engine_declared.get(ancestor)
            if known is None:
                continue
            for field in self.db.structures[ancestor]['fields']:
                name = field['name'].lower()
                if name not in known and name not in taken and IDENT.match(field['name']) and field['name'] not in KEYWORDS:
                    taken.add(name)
                    out.append((field, ancestor) if owners else field)
        return out

    def editor_path(self, path):
        """The path a class has in the editor: its own, or the stub module's for a moved engine class."""
        return f'/Script/{self.relocated[path]}.{path.rsplit(".", 1)[1]}' if path in self.relocated else path

    def note(self, text):
        if text not in self.notes:
            self.notes.append(text)

    def select(self, roots, full=False):
        """Fix the set of classes, structs and enums to write."""
        if full:
            roots = [p for p, s in self.db.structures.items() if s['kind'] == 'Class' and
                     p.startswith('/Script/') and not in_engine(p)]
        for root in roots:
            if root not in self.db.structures:
                self.note(f'{root} is not in the capture')
                continue
            for path in self.db.chain(root):
                if path.startswith('/Script/') and (not in_engine(path) or self.fork_class(path, root)):
                    self.classes.add(path)
        if full:
            self.structs.update(p for p, s in self.db.structures.items() if s['kind'] == 'ScriptStruct' and
                                p.startswith('/Script/') and not in_engine(p))
            self.enums.update(p for p in self.db.enums if p.startswith('/Script/') and not in_engine(p))
        pending = sorted(self.classes | self.structs)
        seen = set()
        while pending:
            path = pending.pop()
            if path in seen:
                continue
            seen.add(path)
            record = self.db.structures[path]
            parent = record['super_path']
            if record['kind'] == 'ScriptStruct' and parent and parent in self.db.structures and \
                    (not in_engine(parent) or self.fork_type('structs', parent, path)):
                self.structs.add(parent)
                pending.append(parent)
            for field in record['fields'] + self.fork_fields(path):
                for node in reflection.walk(field['type_detail']):
                    ref = node.get('struct_path')
                    if ref and (not in_engine(ref) or self.fork_type('structs', ref, path)):
                        if ref in self.db.structures:
                            self.structs.add(ref)
                            pending.append(ref)
                        else:
                            self.note(f'{path}.{field["name"]}: struct {ref} is not in the capture')
                    ref = node.get('enum_path')
                    if ref and (not in_engine(ref) or self.fork_type('enums', ref, path)):
                        if ref in self.db.enums:
                            self.enums.add(ref)
                        else:
                            self.note(f'{path}.{field["name"]}: enum {ref} is not in the capture')
                    ref = node.get('signature_function')
                    if ref and ref in self.db.functions:
                        for parameter in self.db.functions[ref].get('script_signature', {}).get('parameters', []):
                            for inner in reflection.walk(parameter['type_detail']):
                                for key, bucket in (('struct_path', self.structs), ('enum_path', self.enums)):
                                    target = inner.get(key)
                                    if target and not in_engine(target) and (target in self.db.structures or target in self.db.enums):
                                        bucket.add(target)
                                        if key == 'struct_path':
                                            pending.append(target)
        # How each enum is stored decides how it is declared.
        for path in sorted(self.classes | self.structs):
            for field in self.db.structures[path]['fields'] + self.fork_fields(path):
                self._enum_uses(field['type_detail'])
        for function in self.db.functions.values():
            for parameter in function.get('script_signature', {}).get('parameters', []) or []:
                self._enum_uses(parameter['type_detail'])
        self._check_names()
        return self

    def _enum_uses(self, detail):
        for node in reflection.walk(detail):
            ref = node.get('enum_path')
            if not ref:
                continue
            if node['kind'] == 'EnumProperty':
                self.enum_use[ref].add('enum')
                self.enum_width[ref] = UNDERLYING.get(node.get('underlying', {}).get('kind'), 'uint8')
            elif node['kind'] == 'ByteProperty':
                self.enum_use[ref].add('byte')

    def _check_names(self):
        """Things that would not compile, found before Unreal sees them."""
        cpp = collections.defaultdict(list)
        for path in sorted(self.classes | self.structs):
            cpp[self.cpp_name(path)].append(path)
        for path in sorted(self.enums):
            cpp[path.rsplit('.', 1)[1]].append(path)
        for name, paths in cpp.items():
            if len(paths) > 1:
                self.note(f'C++ name {name} is used by {paths}: only one can be declared')
        engine = {record['cpp'].split('::')[0]: key for kind in ('classes', 'structs', 'enums')
                  for key, record in self.index[kind].items()}
        for name, paths in cpp.items():
            if name in engine:
                self.note(f'C++ name {name} of {paths[0]} is also the engine type {engine[name]}')
                # UnrealHeaderTool refuses two reflected types of one name, and with the launcher engine
                # every engine plugin is parsed whether the project uses it or not. A struct is left
                # out; a property of that type is then skipped with its own note.
                for path in paths:
                    if path in self.structs:
                        self.structs.discard(path)
                        self.note(f'{path} is not declared: the engine has a type of that name')
        for path in sorted(self.classes | self.structs):
            own = {}
            for field in self.db.structures[path]['fields']:
                name = field['name']
                if not IDENT.match(name) or name in KEYWORDS:
                    self.note(f'{path}.{name}: not a C++ identifier, property skipped')
                if name.lower() in own:
                    self.note(f'{path}.{name}: same name as {own[name.lower()]} apart from case, property skipped')
                own.setdefault(name.lower(), name)
            for parent in self.db.chain(path)[1:]:
                for field in self.db.structures[parent]['fields']:
                    if field['name'].lower() in own:
                        self.note(f'{path}.{own[field["name"].lower()]}: also declared by parent {parent}, property skipped')

    def skipped(self, path, field):
        name = field['name']
        if not IDENT.match(name) or name in KEYWORDS:
            return True
        for other in self.db.structures[path]['fields']:
            if other is field:
                break
            if other['name'].lower() == name.lower():
                return True
        return any(f['name'].lower() == name.lower() for parent in self.db.chain(path)[1:]
                   for f in self.db.structures[parent]['fields'])

    # ---- names -----------------------------------------------------------------------------

    def cpp_name(self, path):
        """C++ name of a class or struct path."""
        name = path.rsplit('.', 1)[1]
        record = self.db.structures.get(path)
        if self.external(path):
            key = module_of(path) + '.' + name
            for kind in ('classes', 'structs'):
                if key in self.index[kind] and (record is None or (record['kind'] == 'Class') == (kind == 'classes')):
                    return self.index[kind][key]['cpp']
        if record is not None and record['kind'] == 'ScriptStruct':
            return 'F' + name
        if record is None:
            return 'U' + name
        return ('A' if self.db.is_a(path, ACTOR) else 'U') + name

    def known_class(self, path):
        """The class itself if the stubs or the engine declare it, else its nearest parent that is."""
        for candidate in self.db.chain(path) or [path]:
            if candidate in self.classes or candidate == OBJECT:
                return candidate
            if in_engine(candidate):
                if not self.index['classes'] or module_of(candidate) == 'CoreUObject' or \
                        module_of(candidate) + '.' + candidate.rsplit('.', 1)[1] in self.index['classes']:
                    return candidate
        return OBJECT

    def enum_cpp(self, path, byte):
        """C++ spelling of an enum as a property type."""
        name = path.rsplit('.', 1)[1]
        if self.external(path):
            record = self.index['enums'].get(module_of(path) + '.' + name)
            form = record['form'] if record else self.enum_form(path)
            cpp = record['cpp'] if record else (name + '::Type' if form == 'namespace' else name)
        else:
            form = self.enum_form(path)
            cpp = name + '::Type' if form == 'namespace' else name
        if byte:
            return f'TEnumAsByte<{"enum " if form == "plain" else ""}{cpp}>' if form != 'class' else cpp
        return cpp

    def enum_form(self, path):
        """'class' (enum class), 'namespace' (namespace E { enum Type }) or 'plain' (enum E)."""
        scoped = any('::' in v['name'] for v in self.db.enums.get(path, {}).get('values', []))
        if not scoped:
            return 'plain'
        uses = self.enum_use.get(path, set())
        if 'byte' in uses and 'enum' not in uses:
            return 'namespace'
        if 'byte' in uses:
            self.note(f'{path} is stored both as an enum property and as a byte: declared as enum class')
        return 'class'

    # ---- property types --------------------------------------------------------------------

    def type_of(self, detail, context):
        """(C++ type, Blueprint can hold it) for a type detail. Returns (None, reason) when unsupported."""
        kind = detail['kind']
        if kind in PRIMITIVE:
            return PRIMITIVE[kind], kind not in NOT_BLUEPRINT
        if kind == 'BoolProperty':
            return 'bool', True
        if kind == 'ByteProperty':
            if detail.get('enum_path'):
                return self.enum_cpp(detail['enum_path'], True), True
            return 'uint8', True
        if kind == 'EnumProperty':
            width = UNDERLYING.get(detail.get('underlying', {}).get('kind'), 'uint8')
            if not self.external(detail['enum_path']) and detail['enum_path'] in self.db.enums:
                width = self.enum_storage(detail['enum_path'])
            return self.enum_cpp(detail['enum_path'], False), width == 'uint8'
        if kind == 'StructProperty':
            path = detail['struct_path']
            if not self.external(path) and path not in self.structs:
                return None, f'struct {path} is not in the capture'
            return self.cpp_name(path), True
        if kind in ('ObjectProperty', 'WeakObjectProperty', 'LazyObjectProperty', 'SoftObjectProperty'):
            cpp = self.cpp_name(self.known_class(detail['class_path']))
            wrap = {'ObjectProperty': '{}*', 'WeakObjectProperty': 'TWeakObjectPtr<{}>',
                    'LazyObjectProperty': 'TLazyObjectPtr<{}>', 'SoftObjectProperty': 'TSoftObjectPtr<{}>'}[kind]
            return wrap.format(cpp), kind != 'LazyObjectProperty'
        if kind in ('ClassProperty', 'SoftClassProperty'):
            meta = self.known_class(detail.get('meta_class_path') or OBJECT)
            if kind == 'SoftClassProperty':
                return f'TSoftClassPtr<{self.cpp_name(meta)}>', True
            return ('UClass*' if meta == OBJECT else f'TSubclassOf<{self.cpp_name(meta)}>'), True
        if kind == 'InterfaceProperty':
            path = detail['interface_path']
            if self.known_class(path) != path:
                return 'FScriptInterface', False
            return f'TScriptInterface<I{self.cpp_name(path)[1:]}>', True
        if kind in ('ArrayProperty', 'SetProperty'):
            inner, blueprint = self.type_of(detail['inner'], context)
            if inner is None:
                return None, blueprint
            if kind == 'SetProperty':
                self._hash(detail['inner'])
            return ('TArray<{}>' if kind == 'ArrayProperty' else 'TSet<{}>').format(inner), blueprint
        if kind == 'MapProperty':
            key, key_bp = self.type_of(detail['key'], context)
            value, value_bp = self.type_of(detail['value'], context)
            if key is None or value is None:
                return None, key_bp if key is None else value_bp
            self._hash(detail['key'])
            return f'TMap<{key}, {value}>', key_bp and value_bp
        if kind in DELEGATES:
            name = self.delegate(detail['signature_function'], kind, context)
            return (name, True) if name else (None, f'delegate signature {detail["signature_function"]} is not usable')
        if kind == 'FieldPathProperty':
            return 'TFieldPath<FProperty>', False
        return None, f'unsupported property kind {kind}'

    def _hash(self, detail):
        if detail['kind'] == 'StructProperty' and detail['struct_path'] in self.structs:
            self.hashed.add(detail['struct_path'])

    def delegate(self, signature, kind, context):
        """C++ delegate type for a signature function path, declared once."""
        if signature in self.delegates:
            return self.delegates[signature][0]
        parts = signature.split('.')
        short = parts[-1].replace('__DelegateSignature', '')
        owner = '.'.join(parts[:-1]) if len(parts) == 3 else None
        if in_engine(signature):
            cpp = 'F' + short
            if owner:
                cpp = self.cpp_name(owner) + '::' + cpp
            self.delegates[signature] = (cpp, None, None)
            return cpp
        self.delegates[signature] = (None, None, None)
        function = self.db.functions.get(signature)
        if function is None or 'script_signature' not in function:
            return None
        if kind == 'MulticastSparseDelegateProperty':
            return None
        cpp = 'F' + short
        if any(v[0] == cpp for v in self.delegates.values()):
            cpp = 'F' + parts[-2] + short
        parameters, result = [], None
        for parameter in function['script_signature'].get('parameters') or []:
            text, _ = self.type_of(parameter['type_detail'], context)
            if text is None:
                return None
            if parameter.get('is_return'):
                result = text
                continue
            if parameter.get('is_reference') and parameter.get('is_const'):
                text = f'const {text}&'
            elif parameter.get('is_out') or parameter.get('is_reference'):
                text = f'{text}&'
            name = parameter['name'] if IDENT.match(parameter['name']) and parameter['name'] not in KEYWORDS else f'Param{len(parameters)}'
            parameters += [text, name]
        if len(parameters) // 2 >= len(COUNT):
            return None
        macro = 'DECLARE_DYNAMIC' + ('' if kind == 'DelegateProperty' else '_MULTICAST') + '_DELEGATE' + \
                ('_RetVal' if result else '') + COUNT[len(parameters) // 2]
        if result and kind != 'DelegateProperty':
            return None
        arguments = ([result] if result else []) + [cpp] + parameters
        # Declared in the header of the first kind of type that uses it: structs come before classes.
        self.delegates[signature] = (cpp, f'{macro}({", ".join(arguments)});', module_of(signature) + self.phase)
        return cpp

    def register(self):
        """Fix where each delegate is declared before any text is written: structs first, then classes."""
        for phase, paths in (('Structs', self.structs), ('Classes', self.classes)):
            self.phase = phase
            for path in sorted(paths):
                for field in self.db.structures[path]['fields']:
                    if not self.skipped(path, field):
                        self.type_of(field['type_detail'], path)
                for field in self.fork_fields(path):
                    self.type_of(field['type_detail'], path)
                for name in self.functions_of.get(path, ()):
                    for parameter in self.parameters_of(f'{path}.{name}'):
                        self.type_of(parameter['type_detail'], path)

    # ---- functions -------------------------------------------------------------------------

    def parameters_of(self, full):
        """Every parameter record of a function, the return value last."""
        signature = self.db.functions[full].get('script_signature') or {}
        result = signature.get('return_property')
        return [p for p in signature.get('parameters') or [] if not p.get('is_return')] + ([result] if result else [])

    def function_text(self, path, name, owner, interface=False, implementable=False):
        """(header lines, source lines) for one reflected function of a class, or None with a note.

        owner: the C++ class the function is a member of (the I class for an interface).
        implementable: the interface has Blueprint events, so its other functions cannot be callable.
        """
        full = f'{path}.{name}'
        function = self.db.functions[full]
        flags = int(function['flags'], 16)
        if (function.get('script_signature') or {}).get('status') != 'observed':
            self.note(f'{full}: signature not observed, function skipped')
            return None
        if not IDENT.match(name) or name in KEYWORDS:
            self.note(f'{full}: not a C++ identifier, function skipped')
            return None
        chain = self.db.chain(path)
        if any(f['name'].lower() == name.lower() for p in chain for f in self.db.structures[p]['fields']):
            self.note(f'{full}: a property has that name, function skipped')
            return None
        if any(name.lower() == other.lower() for p in chain[1:] for other in self.functions_of.get(p, ())):
            self.note(f'{full}: a parent class declares that function, function skipped')
            return None
        if interface and flags & FUNC_STATIC:
            self.note(f'{full}: static function of an interface, function skipped')
            return None
        result, declared, defined, blueprint, outputs, meta = 'void', [], [], True, False, []
        for number, parameter in enumerate(self.parameters_of(full)):
            cpp, usable = self.type_of(parameter['type_detail'], path)
            if cpp is None or parameter.get('array_dim', 1) > 1:
                self.note(f'{full}: parameter {parameter["name"]}: {usable if cpp is None else "fixed array"}, function skipped')
                return None
            blueprint = blueprint and usable
            if parameter.get('is_return'):
                result, outputs = cpp, True
                continue
            label = parameter['name']
            if not IDENT.match(label) or label in KEYWORDS or label in MACROS:
                label = f'Param{number}'
            detail = parameter['type_detail']
            if parameter.get('is_out') and parameter.get('is_const'):
                form = f'const {cpp}&'
            elif parameter.get('is_out') and parameter.get('is_reference'):
                form = f'UPARAM(ref) {cpp}&'
            elif parameter.get('is_out'):
                form, outputs = f'{cpp}&', True
            elif flags & FUNC_BP_EVENT and detail['kind'] in ('StrProperty', 'ArrayProperty', 'SetProperty', 'MapProperty'):
                # UnrealHeaderTool's event wrappers take strings and containers by const reference.
                form = f'const {cpp}&'
            else:
                form = cpp
            if detail['kind'] == 'StructProperty' and detail['struct_path'] == LATENT:
                meta += ['Latent', f'LatentInfo="{label}"']
            if flags & FUNC_STATIC and label in ('WorldContextObject', 'WorldContext') and detail['kind'] == 'ObjectProperty':
                meta.append(f'WorldContext="{label}"')
            declared.append(f'{form} {label}')
            defined.append(form.replace('UPARAM(ref) ', ''))
        if self.db.is_a(path, '/Script/Engine.AnimInstance'):
            # The game's animation Blueprints update on worker threads, which the compiler only allows
            # when every function their graphs call is marked safe for it. The mark is editor data the
            # game does not keep; that the game's classes kept the setting says the functions had it.
            meta.append('BlueprintThreadSafe')
            if flags & FUNC_PURE and any(text.rsplit(' ', 1)[-1] in ('MachineIndex', 'AssetPlayerIndex') for text in declared):
                # A value function that takes the number of a state machine or of an animation player is
                # one the animation editor fills in itself ("anim getter"): in the game's rule graphs
                # every one of these numbers is the machine, state or transition the rule belongs to.
                meta.append('AnimGetter="true"')
        event = bool(flags & FUNC_BP_EVENT) and blueprint and not flags & FUNC_STATIC
        native = bool(flags & FUNC_NATIVE)
        words = []
        if event:
            words.append('BlueprintNativeEvent' if native else 'BlueprintImplementableEvent')
        if blueprint and not (interface and implementable and not event):
            if flags & FUNC_PURE and outputs:
                words.append('BlueprintPure')
            elif flags & (FUNC_CALLABLE | FUNC_PURE):
                words.append('BlueprintCallable')
                if flags & FUNC_CONST and outputs:
                    words.append('BlueprintPure=false')
        elif flags & (FUNC_CALLABLE | FUNC_PURE | FUNC_BP_EVENT):
            self.note(f'{full}: declared without its Blueprint flags (a type Blueprints cannot hold, or an interface with events)')
        if flags & FUNC_EXEC and not interface:
            words.append('Exec')
        if any(word.startswith('Blueprint') for word in words):
            words.append('Category="Game"')
        if meta:
            words.append(f'meta=({", ".join(meta)})')
        static = 'static ' if flags & FUNC_STATIC else 'virtual ' if interface and not event else ''
        const = ' const' if flags & FUNC_CONST and not flags & FUNC_STATIC else ''
        header = [f'\tUFUNCTION({", ".join(words)})', f'\t{static}{result} {name}({", ".join(declared)}){const};', '']
        if flags & FUNC_PROTECTED and not interface:
            # A protected function: a Blueprint that overrides it gets a protected function too.
            header = ['protected:', *header[:-1], 'public:', '']
        body = '{ return {}; }' if result != 'void' else '{}'
        source = []
        if event and native and not interface:
            source = [f'{result} {owner}::{name}_Implementation({", ".join(defined)}){const} {body}']
        elif not event:
            source = [f'{result} {owner}::{name}({", ".join(defined)}){const} {body}']
        self.function_stats['events' if event else 'functions'] += 1
        return header, source

    def functions_text(self, path, owner, need_types, interface=False):
        """Declarations and definitions for all the reflected functions of one class."""
        names = sorted(self.functions_of.get(path, ()))
        if not names:
            return [], [], set()
        events = [n for n in names if int(self.db.functions[f'{path}.{n}']['flags'], 16) & FUNC_BP_EVENT]
        header, source, forward = [], [], set()
        for name in names:
            made = self.function_text(path, name, owner, interface, bool(events))
            if made is None:
                self.function_stats['skipped'] += 1
                continue
            header += made[0]
            source += made[1]
            for parameter in self.parameters_of(f'{path}.{name}'):
                need_types('classes', parameter['type_detail'])
                forward |= self.forward(parameter['type_detail'])
        return header, source + ([''] if source else []), forward

    def specifiers(self, field, blueprint, owner_is_struct):
        """UPROPERTY specifier text from the game's property flags."""
        flags = int(field['flags'], 16)
        kind = field['type_detail']['kind']
        if flags & F_DEPRECATED:
            return ''
        out, meta = [], []
        delegate = kind in DELEGATES
        static_array = field.get('array_dim', 1) > 1
        edit = bool(flags & F_EDIT)
        # "all" adds the editor flag only. A Blueprint flag the game does not have can be refused by
        # UnrealHeaderTool (engine structs that are not BlueprintType).
        expose_all = self.expose == 'all' and not delegate and not flags & (F_TRANSIENT | F_SKIP)
        if edit or expose_all:
            const = flags & F_EDIT_CONST and edit
            where = 'DefaultsOnly' if flags & F_NO_INSTANCE and edit else 'InstanceOnly' if flags & F_NO_TEMPLATE and edit else 'Anywhere'
            out.append(('Visible' if const else 'Edit') + where)
            if flags & F_FIXED:
                out.append('EditFixedSize')
        if blueprint and not static_array and not delegate:
            if flags & F_BP:
                out.append('BlueprintReadOnly' if flags & F_BP_RO else 'BlueprintReadWrite')
        if delegate and kind != 'DelegateProperty':
            if flags & F_ASSIGNABLE:
                out.append('BlueprintAssignable')
            if flags & F_CALLABLE:
                out.append('BlueprintCallable')
        for bit, word in ((F_TRANSIENT, 'Transient'), (F_DUP_TRANSIENT, 'DuplicateTransient'),
                          (F_TEXT_TRANSIENT, 'TextExportTransient'), (F_NON_PIE_TRANSIENT, 'NonPIEDuplicateTransient'),
                          (F_SAVE_GAME, 'SaveGame'), (F_INTERP, 'Interp'), (F_NO_CLEAR, 'NoClear'),
                          (F_NON_TRANSACTIONAL, 'NonTransactional'), (F_SEARCHABLE, 'AssetRegistrySearchable'),
                          (F_SIMPLE, 'SimpleDisplay'), (F_ADVANCED, 'AdvancedDisplay'), (F_SKIP, 'SkipSerialization')):
            if flags & bit:
                out.append(word)
        if flags & F_PERSISTENT:
            out.append('Instanced')
        elif flags & F_EXPORT and not flags & F_INSTANCED_REF:
            out.append('Export')
        if flags & F_EXPOSE_ON_SPAWN and flags & F_BP and blueprint:
            meta.append('ExposeOnSpawn="true"')
        if any(word.startswith(('Edit', 'Visible', 'Blueprint')) for word in out):
            out.append('Category="Game"')
        if meta:
            out.append(f'meta=({", ".join(meta)})')
        return ', '.join(out)

    def kept(self, path, field):
        """The default subobject a transient property of a class points to when nothing the editor
        saves points to it, or None. The game's file holds what a Blueprint or a level changes on such
        an object (the cook writes every default subobject), but an editor's save follows only saved
        properties, so the object and what was set on it would be gone after the next load. The stub
        gets a second reference to it that only the editor has and saves."""
        record = self.defaults.get(path)
        detail = field['type_detail']
        if not record or detail['kind'] != 'ObjectProperty' or field.get('array_dim', 1) != 1 or \
                not int(field['flags'], 16) & F_TRANSIENT or self.skipped(path, field) or self.type_of(detail, path)[0] is None:
            return None
        value = record.get('own', {}).get(field['name'])
        sub = value.get('sub') if isinstance(value, dict) else None
        if not sub:
            return None
        saved = {f['name'] for f in self.db.structures[path]['fields'] if not int(f['flags'], 16) & F_TRANSIENT}
        held = [v for name, v in record.get('own', {}).items() if name in saved] + list(record.get('inherited', {}).values())
        return None if any(isinstance(v, dict) and v.get('sub') == sub for v in held) else sub

    # ---- runtime layout (UE4 recomp bridge) -------------------------------------------------

    KIND_ALIGN = {'BoolProperty': 1, 'ByteProperty': 1, 'Int8Property': 1, 'Int16Property': 2, 'UInt16Property': 2,
                  'IntProperty': 4, 'UInt32Property': 4, 'FloatProperty': 4, 'NameProperty': 4, 'WeakObjectProperty': 4,
                  'LazyObjectProperty': 4, 'DelegateProperty': 4, 'Int64Property': 8, 'UInt64Property': 8,
                  'DoubleProperty': 8}

    def runtime_align(self, detail, depth=0):
        """Alignment of a property's C++ type on the host (8 for pointers and containers)."""
        kind = detail.get('kind')
        if kind == 'EnumProperty':
            return min(8, max(1, detail.get('element_size') or 1))
        if kind == 'StructProperty' and depth < 8:
            fields = self.db.structures.get(detail.get('struct_path'), {}).get('fields', [])
            return max([self.runtime_align(f['type_detail'], depth + 1) for f in fields] or [4])
        return self.KIND_ALIGN.get(kind, 8)

    def runtime_property_lines(self, path, owner_is_struct=False):
        """The class's properties in the game's offset order, with the game's hidden bytes between
        them as padding members, so translated code finds every member where the game has it."""
        record = self.db.structures[path]
        parent = self.db.structures.get(record['super_path'] or '', {})
        decl, _ = self._plain_property_lines(path, owner_is_struct)
        if owner_is_struct and self.runtime_flattened(path):
            # No C++ base: the parents' properties are this struct's, at their game offsets.
            chain, fields = [], list(record['fields'])
            ancestor = record['super_path']
            while ancestor:
                chain.append(ancestor)
                fields = list(self.db.structures[ancestor]['fields']) + fields
                ancestor = self.db.structures[ancestor].get('super_path')
            for ancestor in reversed(chain):
                decl = self._plain_property_lines(ancestor, True)[0] + decl
            record = dict(record, fields=fields, super_path=None)
            parent = {}
        blocks, current = {}, []
        for line in decl:  # the UPROPERTY line, the declaration, a blank: keyed by member name
            current.append(line)
            m = re.match(r'\t(?:.+?)\s+(\w+)(?:\[\d+\])?(?:\s*:\s*1)?;', line)
            if m and not line.lstrip().startswith('UPROPERTY'):
                blocks[re.sub(r'_DEPRECATED$', '', m[1])] = current
                current = []
        def bitpos(f):
            d = f['type_detail']
            if d['kind'] == 'BoolProperty' and not d.get('native_bool', True) and d.get('byte_mask'):
                return (f['offset'] + (d.get('byte_offset') or 0)) * 8 + d['byte_mask'].bit_length() - 1
            return f['offset'] * 8
        out, cursor, hidden = [], None, 0  # cursor: host bit position after the previous member
        packed = self.runtime_pack_target(path) is not None
        for f in sorted(record['fields'], key=bitpos):
            block = blocks.get(f['name'])
            if not block:
                continue
            bit = bitpos(f)
            is_bit = f['type_detail']['kind'] == 'BoolProperty' and not f['type_detail'].get('native_bool', True)
            if cursor is None:
                cursor = self.runtime_parent_size(record) * 8 if not self.runtime_packed_start(path) else bit
                cursor = min(cursor, bit)
            if is_bit:
                if cursor // 8 != bit // 8:  # a new byte: pad whole bytes to it
                    cursor = -(-cursor // 8) * 8
                gap = bit - cursor
                if gap >= 8:
                    out.append(f'\tuint8 P4Hidden{hidden}[{gap // 8}];')
                    hidden += 1
                    gap %= 8
                if gap:
                    out.append(f'\tuint8 P4HiddenBits{hidden} : {gap};')
                    hidden += 1
                out += block
                cursor = bit + 1
                continue
            align = 8 if packed else self.runtime_align(f['type_detail']) * 8  # packed: every gap is explicit
            natural = -(-cursor // align) * align
            if bit > natural:
                start = -(-cursor // 8) * 8
                if bit - start >= 8:
                    out.append(f'\tuint8 P4Hidden{hidden}[{(bit - start) // 8}];')
                    hidden += 1
            elif bit < natural:
                self.note(f'{path}.{f["name"]}: the game has it {(natural - bit) // 8} bytes before C++ would place it')
            out += block
            cursor = bit + f['storage_size'] * 8
        end = (self.runtime_pack_target(path) if packed else record['observed_size']) * 8
        if cursor is None:  # no properties of its own: the game's size still holds hidden members
            cursor = self.runtime_parent_size(record) * 8
        if end - (-(-cursor // 8) * 8) >= 8:
            out.append(f'\tuint8 P4HiddenTail[{(end - (-(-cursor // 8) * 8)) // 8}];')
        return out, []

    def runtime_parent_size(self, record):
        """Where the class's own members start on the host: its parent's size there. An engine
        parent's comes from the patched engine's layout dump (it can be smaller than the game's,
        whose tail the game's subclasses reuse); a game parent has the game's size."""
        parent_path = record['super_path'] or ''
        parent = self.db.structures.get(parent_path, {})
        if parent_path in self.classes or parent_path in self.structs or not parent:
            target = self.runtime_pack_target(parent_path)
            return target if target is not None else parent.get('observed_size') or 0
        if not hasattr(self, '_host_layouts'):
            private = Path(__file__).resolve().parents[2] / 'private/ue4-recomp-bridge'
            f = private / 'layout/windows-layouts-msvc-patched.json'
            self._host_layouts = json.loads(f.read_text(encoding='utf-8')) if f.exists() else {}
            # Sizes the export build measured where the clang-cl dump differs (a packed class's
            # alignment): carrot4_gen.py's layout report, read back by export_layout_sizes.py.
            m = private / 'classes/host-sizes.json'
            self._host_sizes = json.loads(m.read_text(encoding='utf-8')) if m.exists() else {}
        cpp = self.cpp_name(parent_path)
        host = self._host_sizes.get(cpp, self._host_layouts.get(cpp, {}).get('size'))
        if host is None:
            self.note(f'{parent_path}: no host layout measured, the game size is used')
        return host if host is not None else parent.get('observed_size') or 0

    def runtime_pack_target(self, path):
        """Where a game class's subclasses start when that is inside its size (Clang reuses a
        non-POD base's tail padding): the class is packed and ends there, or None. Structs are
        flattened instead (runtime_flattened): they are also embedded whole."""
        if not hasattr(self, '_pack_targets'):
            self._pack_targets = {}
            for child in self.classes:
                record = self.db.structures.get(child, {})
                parent = record.get('super_path')
                fields = record.get('fields') or []
                if parent not in self.classes or not fields:
                    continue
                first = min(f['offset'] for f in fields)
                size = self.db.structures[parent].get('observed_size') or 0
                if first < size:
                    self._pack_targets[parent] = min(self._pack_targets.get(parent, first), first)
        return self._pack_targets.get(path)

    def runtime_flattened(self, path):
        """A game struct whose own members start inside its parent struct's tail padding (Clang
        reuses it; MSVC cannot): declared without the C++ base, with the parents' properties."""
        record = self.db.structures.get(path, {})
        parent = self.db.structures.get(record.get('super_path') or '', {})
        fields = record.get('fields') or []
        return bool(parent and fields and parent.get('kind') == 'ScriptStruct'
                    and min(f['offset'] for f in fields) < (parent.get('observed_size') or 0))

    def runtime_packed_start(self, path):
        return False

    def property_lines(self, path, owner_is_struct):
        if self.runtime is not None and (path in self.runtime or owner_is_struct):
            # Game structs are embedded in game objects and passed to game code: same layout rule.
            lines, _ = self.runtime_property_lines(path, owner_is_struct)
            _, init = self._plain_property_lines(path, owner_is_struct)
            return lines, init
        return self._plain_property_lines(path, owner_is_struct)

    def _plain_property_lines(self, path, owner_is_struct):
        """Declaration lines for a type's own properties, and the names to value-initialize."""
        lines, init = [], []
        added = [] if owner_is_struct else self.fork_fields(path)
        if added:
            self.note(f'{path} declares {len(added)} properties the game\'s engine adds to its engine parents')
        for field in self.db.structures[path]['fields'] + added:
            name = field['name']
            if not any(field is other for other in added) and self.skipped(path, field):
                lines.append(f'\t// {name}: skipped, see the generator notes')
                continue
            detail = field['type_detail']
            cpp, blueprint = self.type_of(detail, path)
            if cpp is None:
                self.note(f'{path}.{name}: {blueprint}, property skipped')
                lines.append(f'\t// {name}: skipped, {blueprint}')
                continue
            flags = int(field['flags'], 16)
            notes = [word for bit, word in ((F_NET, 'replicated in the game'), (F_CONFIG, 'config in the game')) if flags & bit]
            member = name + '_DEPRECATED' if flags & F_DEPRECATED else name
            dim = field.get('array_dim', 1)
            if detail['kind'] == 'BoolProperty' and not detail.get('native_bool', True) and dim == 1:
                declaration = f'uint8 {member} : 1;'
            else:
                declaration = f'{cpp} {member}{f"[{dim}]" if dim > 1 else ""};'
            lines.append(f'\tUPROPERTY({self.specifiers(field, blueprint, owner_is_struct)})')
            lines.append(f'\t{declaration}' + (f'  // {", ".join(notes)}' if notes else ''))
            lines.append('')
            if not owner_is_struct and not any(field is other for other in added) and self.kept(path, field):
                lines += ['#if WITH_EDITORONLY_DATA',
                          f'\t// Not the game\'s: {name} is transient, so an editor\'s save would not reach the object. Not cooked.',
                          '\tUPROPERTY()', f'\t{cpp} {KEPT}{member};', '#endif', '']
            if detail['kind'] in VALUE_INIT:
                init.append(member + '()')
            elif detail['kind'] == 'StructProperty' and detail['struct_path'] in FORCE_INIT and dim == 1:
                init.append(member + '(ForceInit)')
        return lines, init

    # ---- ordering and includes -------------------------------------------------------------

    def ordered(self, paths, depends):
        """Paths so that each comes after what it depends on. Ties and cycles go by name."""
        paths = sorted(paths)
        done, out, active = set(), [], set()

        def visit(path):
            if path in done:
                return
            if path in active:
                self.note(f'{path} depends on itself through other types: declaration order may be wrong')
                return
            active.add(path)
            for other in sorted(depends(path)):
                if other in paths_set and other != path:
                    visit(other)
            active.discard(path)
            done.add(path)
            out.append(path)
        paths_set = set(paths)
        for path in paths:
            visit(path)
        return out

    def struct_depends(self, path):
        record = self.db.structures[path]
        out = {record['super_path']} if record['super_path'] else set()
        for field in record['fields']:
            for node in reflection.walk(field['type_detail']):
                if node.get('struct_path'):
                    out.add(node['struct_path'])
        return out

    def engine_include(self, kind, path):
        if not self.external(path) or module_of(path) == 'CoreUObject':
            return None
        record = self.index[kind].get(module_of(path) + '.' + path.rsplit('.', 1)[1])
        if record is None:
            if self.index[kind]:
                self.note(f'{path}: the engine header index has no {kind[:-1].rstrip("e")} of that name, no #include written')
            return None
        return record['include']

    # ---- output ----------------------------------------------------------------------------

    def modules(self):
        return sorted({self.home(p) for p in self.classes | self.structs | self.enums})

    def files(self):
        """{path relative to the plugin folder: text}."""
        out = {}
        modules = self.modules()
        self.register()
        uses = {m: set() for m in modules}  # module -> modules it needs
        bodies = {m: self.module_text(m, uses[m]) for m in modules}
        for module in modules:
            for other in sorted(uses[module] & set(modules)):
                if module in uses[other] and module < other:
                    self.note(f'modules {module} and {other} need each other: not buildable as separate modules')
            enums, structs, classes, source = bodies[module]
            base = f'Source/{module}/'
            out[base + f'Public/{module}StubEnums.h'] = enums
            out[base + f'Public/{module}StubStructs.h'] = structs
            out[base + f'Public/{module}StubClasses.h'] = classes
            out[base + f'Private/{module}Module.cpp'] = source
            # SSpacer, which the empty widgets return, lives in Slate (UMG asks for it instead of SNullWidget).
            empty_widget = {'Slate', 'SlateCore'} if EMPTY_WIDGET[1] in classes else set()
            deps = sorted((uses[module] | {'Core', 'CoreUObject', 'Engine'} | ({CORE} if self.defaults else set()) |
                           empty_widget) - {module})
            out[base + f'{module}.Build.cs'] = BUILD.format(module=module, deps=', '.join(f'"{d}"' for d in deps))
        out[f'{self.plugin}.uplugin'] = json.dumps(dict(
            FileVersion=3, Version=1, VersionName='0.1', FriendlyName='Pikmin 4 stub classes',
            Description='Classes with the names and properties of the game\'s own, without behavior, so levels '
                        'that use them can be edited and cooked. The game loads its real classes.',
            Category='Pikmin 4', CanContainContent=False, Installed=False,
            Modules=[dict(Name=m, Type='Runtime', LoadingPhase='PreDefault')
                     for m in ([CORE] if self.defaults else []) + modules]), indent='\t') + '\n'
        if self.defaults:
            for source in sorted((HERE / 'core_module').rglob('*')):
                if source.is_file():
                    out[f'Source/{CORE}/' + source.relative_to(HERE / 'core_module').as_posix()] = source.read_text(encoding='utf-8')
            out[f'Source/{CORE}/Private/P4EngineDefaults.cpp'] = self.engine_default_rows()
            # The rows as data, for the editor check (editor_check.py compares each with the loaded class).
            config = [[self.editor_path(path), '', name, text, 'Config'] for path, values in sorted(self.default_config.items())
                      for name, text in values.items()]
            out['default-rows.json'] = json.dumps(self.default_table + config, indent=0) + '\n'
            # For the project's Config/DefaultEngine.ini (run_editor_check.py adds it to its test project).
            lines = ["; The game's values for config properties of the stub classes. Add to Config/DefaultEngine.ini."]
            for path, values in sorted(self.default_config.items()):
                lines += ['', f'[{self.editor_path(path)}]']
                for name, text in values.items():
                    elements = self.default_config_elements.get((path, name))
                    lines += [f'{name}={text}'] if elements is None else [f'+{name}={element}' for element in elements]
            out['default-config.ini'] = '\n'.join(lines) + '\n'
        return out

    def module_text(self, module, uses):
        mine = lambda paths: [p for p in paths if self.home(p) == module]  # noqa: E731
        includes = dict(enums=set(), structs=set(), classes=set(), source=set())

        def need(bucket, kind, path):
            if not self.external(path):
                other = self.home(path)
                if other != module:
                    uses.add(other)
                    includes[bucket].add(f'{other}Stub{"Classes" if kind == "classes" else "Structs" if kind == "structs" else "Enums"}.h')
                return
            if module_of(path) != 'CoreUObject':
                uses.add(module_of(path))
            include = self.engine_include(kind, path)
            if include:
                includes[bucket].add(include)

        def need_types(bucket, detail):
            for node in reflection.walk(detail):
                if node.get('struct_path'):
                    need(bucket, 'structs', node['struct_path'])
                if node.get('enum_path'):
                    need(bucket, 'enums', node['enum_path'])
                signature = node.get('signature_function')
                if signature and in_engine(signature) and len(signature.split('.')) == 3:
                    need(bucket, 'classes', signature.rsplit('.', 1)[0])
                elif signature and in_engine(signature):
                    record = self.index['delegates'].get(module_of(signature) + '.' + signature.rsplit('.', 1)[1].replace('__DelegateSignature', ''))
                    if record:
                        includes[bucket].add(record['include'])
                        uses.add(module_of(signature))
                elif signature and module_of(signature) != module and self.delegates.get(signature, (None,))[0]:
                    uses.add(module_of(signature))
                    includes[bucket].add(self.delegates[signature][2].replace(module_of(signature), module_of(signature) + 'Stub', 1) + '.h')
                for key in ('class_path', 'meta_class_path', 'interface_path'):
                    if node.get(key):
                        target = self.known_class(node[key])
                        if module_of(target) not in (None, module, 'CoreUObject'):
                            uses.add(module_of(target))
                        if key == 'interface_path' and target == node[key]:
                            need(bucket, 'classes', target)

        # Enums.
        text = [HEADER.format(what='Enums', module=module), '#include "CoreMinimal.h"',
                f'#include "{module}StubEnums.generated.h"', '']
        for path in sorted(mine(self.enums)):
            text += self.enum_text(path)
        enums = reflected('\n'.join(text) + '\n')

        # Structs.
        body, forward = [], set()
        for path in self.ordered(mine(self.structs), self.struct_depends):
            record = self.db.structures[path]
            parent = record['super_path']
            if parent:
                need('structs', 'structs', parent)
            lines, init = self.property_lines(path, True)
            for field in record['fields']:
                need_types('structs', field['type_detail'])
                forward |= self.forward(field['type_detail'])
            cpp = self.cpp_name(path)
            body.append('USTRUCT(BlueprintType)')
            flat = self.runtime is not None and self.runtime_flattened(path)
            body.append(f'struct {cpp}' + (f' : public {self.cpp_name(parent)}' if parent and not flat else ''))
            body += ['{', '\tGENERATED_BODY()', '']
            body += lines
            if init:
                body.append(f'\t{cpp}() : ' + ', '.join(init) + ' {}')
            if path in self.hashed:
                body += [f'\tbool operator==(const {cpp}& Other) const {{ return StaticStruct()->CompareScriptStruct(this, &Other, 0); }}',
                         f'\tfriend uint32 GetTypeHash(const {cpp}&) {{ return 0; }}']
            body += ['};', '']
            if path in self.zero_constructor and not self.db.is_a(path, TICK):
                body += [f'template<> struct TStructOpsTypeTraits<{cpp}> : public TStructOpsTypeTraitsBase2<{cpp}>',
                         '{', '\tenum { WithZeroConstructor = true };', '};', '']
            if self.db.is_a(path, TICK):
                # A tick function cannot be copied, as the engine's own (FActorTickFunction) declare.
                body += [f'template<> struct TStructOpsTypeTraits<{cpp}> : public TStructOpsTypeTraitsBase2<{cpp}>',
                         '{', '\tenum { WithCopy = false };', '};', '']
        delegates_in_structs = sorted(v[1] for v in self.delegates.values() if v[1] and v[2] == module + 'Structs')
        text = [HEADER.format(what='Structs', module=module), '#include "CoreMinimal.h"']
        text += [f'#include "{i}"' for i in sorted(includes['structs'])]
        text += [f'#include "{module}StubEnums.h"', f'#include "{module}StubStructs.generated.h"', '']
        text += [f'class {name};' for name in sorted(forward)] + ['']
        text += delegates_in_structs + [''] + body
        structs = reflected('\n'.join(text) + '\n')

        # Classes.
        body, forward, source = [], set(), []
        for path in self.ordered(mine(self.classes), lambda p: {self.db.structures[p]['super_path']}):
            record = self.db.structures[path]
            parent = record['super_path'] or OBJECT
            need('classes', 'classes', parent)
            lines, _ = self.property_lines(path, False)
            for field in record['fields'] + self.fork_fields(path):
                need_types('classes', field['type_detail'])
                forward |= self.forward(field['type_detail'])
            cpp = self.cpp_name(path)
            parent_cpp = self.cpp_name(parent)
            api = f'{module.upper()}_API '
            if self.db.is_a(path, INTERFACE):
                declared, defined, more = self.functions_text(path, 'I' + cpp[1:], need_types, interface=True)
                forward |= more
                source += defined
                # An interface whose functions are plain calls cannot be implemented by a Blueprint
                # (UnrealHeaderTool asks for the events form otherwise).
                closed = declared and not any('Event' in line for line in declared)
                body += ['UINTERFACE(BlueprintType' + (', meta=(CannotImplementInterfaceInBlueprint)' if closed else '') + ')',
                         f'class {api}{cpp} : public {parent_cpp}', '{',
                         '\tGENERATED_BODY()', '};', '',
                         f'class {api}I{cpp[1:]}' + (f' : public I{parent_cpp[1:]}' if parent != INTERFACE else ''),
                         '{', '\tGENERATED_BODY()', 'public:'] + declared + ['};', '']
                continue
            component = self.db.is_a(path, COMPONENT)
            packed = self.runtime is not None and self.runtime_pack_target(path) is not None
            if packed:  # its game subclasses start inside its tail padding (Itanium reuses it, MSVC does not)
                body.append('#pragma pack(push, 1)')
            body.append('UCLASS(Blueprintable, BlueprintType' + (', meta=(BlueprintSpawnableComponent)' if component else '') + ')')
            body += [f'class {api}{cpp} : public {parent_cpp}', '{', '\tGENERATED_BODY()', 'public:']
            if parent not in self.classes:
                for ancestor in self.db.chain(parent):
                    body += UNEXPORTED_VIRTUALS.get(ancestor, [])
                if parent in ABSTRACT_WIDGET_PARENTS:
                    body += EMPTY_WIDGET
                body += self.track_functions(path, parent)
            made = self.subobjects.get(path, [])
            if self.runtime is not None and path in self.runtime:
                # The game's own constructor makes the subobjects and sets the defaults.
                # #if CPP: UnrealHeaderTool skips the block (plain C++ overrides), the compiler sees it.
                body += ['#if CPP', f'\tP4_GAME_CLASS_BODY_{cpp}', '#endif',
                         f'\t{cpp}(const FObjectInitializer& ObjectInitializer);', '']
                base_call = 'Super()' if parent not in self.classes and self.default_constructor_only(parent) else 'Super(ObjectInitializer)'
                if base_call != 'Super()' and self.runtime[path].get('subobject_overrides'):
                    base_call = f'Super(P4SubobjectOverrides_{cpp}(ObjectInitializer))'
                source += [f'{cpp}::{cpp}(const FObjectInitializer& ObjectInitializer) : {base_call}', '{',
                           f'\tP4RunGameConstructor_{cpp}(this, ObjectInitializer);' if self.runtime[path].get('ctors') else '',
                           '}', '']
            elif path in self.defaults:
                made = self.constructor_from_defaults(path, cpp, lambda p: need('source', 'classes', p))
                if made:
                    body += [f'\t{cpp}(const FObjectInitializer& ObjectInitializer);', '']
                    source += made
            elif made:
                body += [f'\t{cpp}(const FObjectInitializer& ObjectInitializer);', '']
                source += self.constructor(path, cpp, made, lambda p: need('source', 'classes', p))
            body += lines
            body += NATIVE_SERIALIZERS.get(path, [])
            declared, defined, more = self.functions_text(path, cpp, need_types)
            forward |= more
            body += declared
            source += defined
            body += ['};', '']
            if packed:
                body += ['#pragma pack(pop)', '']
        class_delegates = sorted(v[1] for v in self.delegates.values() if v[1] and v[2] == module + 'Classes')
        text = [HEADER.format(what='Classes', module=module), '#include "CoreMinimal.h"']
        if any(path.startswith(f'/Script/{module}.') for path in NATIVE_SERIALIZERS):
            text.append('#include "Serialization/BulkData.h"')
        text += [f'#include "{i}"' for i in sorted(includes['classes'])]
        if self.runtime is not None:
            text.append('#include "P4Carrot4Overrides.h"')  # per-class bodies from carrot4_gen.py
        if any(EMPTY_WIDGET[1] == line for line in body):
            text.append('#include "Widgets/Layout/SSpacer.h"')
        text += [f'#include "{module}StubStructs.h"', f'#include "{module}StubClasses.generated.h"', '']
        text += [f'class {name};' for name in sorted(forward)] + ['']
        text += class_delegates + [''] + body
        classes = reflected('\n'.join(text) + '\n')

        text = [HEADER.format(what='Module', module=module).replace('#pragma once\n', ''),
                f'#include "{module}StubClasses.h"']
        text += [f'#include "{i}"' for i in sorted(includes['source'])]
        if self.defaults:
            text.append('#include "P4StubDefaults.h"')
        if any('UP4RecoveredDirectionalLightComponent::StaticClass()' in line for line in source):
            text.append('#include "P4RecoveredDirectionalLightComponent.h"')
        if any('UP4RecoveredPostProcessComponent::StaticClass()' in line for line in source):
            text.append('#include "P4RecoveredPostProcessComponent.h"')
        text += ['#include "Modules/ModuleManager.h"', '', f'IMPLEMENT_MODULE(FDefaultModuleImpl, {module});', '']
        text += source
        return enums, structs, classes, '\n'.join(text) + '\n'

    def default_constructor_only(self, path):
        """An engine class that declares a constructor without an FObjectInitializer and none with
        one (UAssetManager): a subclass constructor calls Super(), not Super(ObjectInitializer)."""
        include = self.engine_include('classes', path)
        if not include:
            return False
        if not hasattr(self, '_engine_headers'):
            import os
            source = Path(os.environ.get('P4_UE_ROOT', r'C:\Program Files\Epic Games\UE_4.26')) / 'Engine/Source'
            self._engine_headers = collections.defaultdict(list)
            for top in ('Runtime', 'Developer'):
                for h in (source / top).rglob('*.h'):
                    self._engine_headers[h.name].append(h)
        cpp = self.cpp_name(path)
        for h in self._engine_headers.get(Path(include).name, []):
            if not h.as_posix().endswith(include):
                continue
            text = h.read_text(encoding='utf-8-sig', errors='replace')
            if re.search(rf'\b{cpp}\s*\(\s*const\s+FObjectInitializer', text):
                return False
            return bool(re.search(rf'^\s*(?:\w+_API\s+)?{cpp}\s*\(\s*\)\s*;', text, re.M))
        return False

    def track_functions(self, path, parent):
        """A sequencer track class directly under the engine's abstract track: the functions every
        track must have (the engine's own stop the editor when called), over the class's own list of
        sections."""
        if parent not in ('/Script/MovieScene.MovieSceneTrack', '/Script/MovieScene.MovieSceneNameableTrack'):
            return []
        held = next((f['name'] for f in self.db.structures[path]['fields'] if f['type_detail'].get('kind') == 'ArrayProperty' and
                     (f['type_detail'].get('inner') or {}).get('class_path') == '/Script/MovieScene.MovieSceneSection'), None)
        if held is None:
            return []
        return [f'	virtual bool IsEmpty() const override {{ return {held}.Num() == 0; }}',
                '	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override { return true; }',
                f'	virtual void AddSection(UMovieSceneSection& Section) override {{ {held}.Add(&Section); }}',
                '	virtual UMovieSceneSection* CreateNewSection() override { return nullptr; }',
                f'	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override {{ return {held}; }}',
                f'	virtual bool HasSection(const UMovieSceneSection& Section) const override {{ return {held}.Contains(&Section); }}',
                f'	virtual void RemoveSection(UMovieSceneSection& Section) override {{ {held}.Remove(&Section); }}',
                f'	virtual void RemoveSectionAt(int32 SectionIndex) override {{ {held}.RemoveAt(SectionIndex); }}',
                f'	virtual void RemoveAllAnimationData() override {{ {held}.Empty(); }}', '']

    def forward(self, detail):
        """Class names to forward declare for the object references of a type detail."""
        out = set()
        for node in reflection.walk(detail):
            for key in ('class_path', 'meta_class_path'):
                if node.get(key):
                    target = self.known_class(node[key])
                    if target != OBJECT:
                        out.add(self.cpp_name(target))
        return out

    def constructor(self, path, cpp, made, need):
        """Constructor that creates the default subobjects the cooked data shows."""
        lines = [f'{cpp}::{cpp}(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)', '{']
        declared = {f['name'].lower(): f['name'] for owner in self.db.chain(path)
                    if owner in self.classes for f in self.db.structures[owner]['fields']}
        local = {item['name']: 'Made' + re.sub(r'\W', '_', item['name']) for item in made}
        # Roots first, then parents before what attaches to them.
        order, left = [], sorted(made, key=lambda item: (not item.get('root'), item['name']))
        while left:
            ready = [i for i in left if not i.get('attach') or i['attach'] not in local or
                     any(o['name'] == i['attach'] for o in order)] or left[:1]
            order.append(ready[0])
            left.remove(ready[0])
        for item in order:
            cls = self.known_class(item['cls'])
            need(cls)
            name, var, kind = item['name'], local[item['name']], self.cpp_name(cls)
            # A parent class that is not a stub may already have made it: the cooked data does not
            # say which class in the chain creates a subobject.
            lines.append(f'\t{kind}* {var} = Cast<{kind}>(GetDefaultSubobjectByName(TEXT("{name}")));')
            lines.append(f'\tif (!{var})')
            lines.append('\t{')
            lines.append(f'\t\t{var} = {self.make_subobject(path, cls, kind, name)};')
            if item.get('root'):
                lines.append(f'\t\tRootComponent = {var};')
            elif item.get('attach') in local:
                lines.append(f'\t\t{var}->SetupAttachment({local[item["attach"]]});')
            lines.append('\t}')
            for prop in item.get('properties', []):
                if prop.lower() in declared:
                    lines.append(f'\t{declared[prop.lower()]} = {var};')
        return lines + ['}', '']

    @staticmethod
    def make_subobject(owner, cls, kind, name):
        # EnvController's two native light subobjects use the plain Engine class in
        # the game. Its added fields need genuine child storage in the stock editor.
        # Keep the declared pointer type and stock native light behavior intact.
        if owner == '/Script/Carrot4.EnvController' and cls == '/Script/Engine.DirectionalLightComponent' and name in ('SunLight', 'StaticSunLight'):
            return (f'Cast<{kind}>(CreateDefaultSubobject(TEXT("{name}"), {kind}::StaticClass(), '
                    'UP4RecoveredDirectionalLightComponent::StaticClass(), true, false))')
        if owner == '/Script/Carrot4.EnvController' and cls == '/Script/Engine.PostProcessComponent' and name == 'PostProcess':
            return (f'Cast<{kind}>(CreateDefaultSubobject(TEXT("{name}"), {kind}::StaticClass(), '
                    'UP4RecoveredPostProcessComponent::StaticClass(), true, false))')
        return f'CreateDefaultSubobject<{kind}>(TEXT("{name}"))'

    def default_rows(self, path):
        """(subobject, property path, value, kind) for what the game's class default object of a class
        holds that its stub would not: the class's own values, the inherited values it changes, and
        what its default subobjects change."""
        record = self.defaults[path]
        rows, skipped = [], []
        own = {name.lower(): value for name, value in record.get('own', {}).items()}
        for field in self.db.structures[path]['fields']:
            name = field['name']
            if name.lower() not in own or self.skipped(path, field) or int(field['flags'], 16) & F_DEPRECATED or \
                    self.type_of(field['type_detail'], path)[0] is None:
                continue
            value = own[name.lower()]
            # A struct is written whole: an engine struct's constructor may not leave zeros.
            if not zero(value) or (isinstance(value, dict) and 'fields' in value):
                leaves(name, value, '', rows, skipped)
        for field in self.db.structures[path]['fields']:
            sub = self.kept(path, field)
            if sub:
                rows.append(('', KEPT + field['name'], sub, 'Subobject'))
        for key, value in record.get('inherited', {}).items():
            owner, name = key.split('.', 1)
            if key in STRUCTURE:
                continue
            if self.is_config(path, owner, name):
                # These are the native class's default facts. Unreal's later
                # LoadConfig overrides them only when an actual config key is
                # present. Keep them in the constructor so a runtime-only SDK
                # does not depend on the private editor-check INI below.
                leaves(name, value, '', rows, skipped)
                text = next((value[k] for k in ('name', 'str', 'soft') if isinstance(value, dict) and k in value), None)
                text = literal(value) if text is None else text
                if text is None:
                    skipped.append((name, 'a config value that is not plain text'))
                else:
                    self.default_config.setdefault(path, {})[name] = text
                    if isinstance(value, dict) and 'array' in value:
                        # An ini file holds an array as one line per element.
                        self.default_config_elements[(path, name)] = [literal(element) for element in value['array']]
                continue
            leaves(name, value, '', rows, skipped)
        changed = {key.split('.', 1)[1].lower() for key in record.get('inherited', {})}
        for field, ancestor in self.fork_fields(path, owners=True):
            value = self.defaults.get(ancestor, {}).get('own', {}).get(field['name'])
            if value is not None and field['name'].lower() not in changed and not zero(value) and \
                    self.type_of(field['type_detail'], path)[0] is not None:
                leaves(field['name'], value, '', rows, skipped)
        before = self.subobjects_before(path)
        for sub in record.get('subobjects', []):
            earlier = before.get(sub['name'])
            earlier = earlier.get('changed', {}) if earlier and earlier['cls'] == sub['cls'] else {}
            for key, value in sub.get('changed', {}).items():
                if key not in STRUCTURE and not (key in earlier and same(earlier[key], value)):
                    leaves(key.split('.', 1)[1], value, sub['name'], rows, skipped)
            for key in earlier:
                if key not in sub.get('changed', {}) and key not in STRUCTURE:
                    skipped.append((sub['name'] + '.' + key, 'the parent class changes it and this class has the default again'))
        for where, why in skipped:
            self.default_stats['not written: ' + why.split(' / ')[0]] += 1
            self.note(f'{path} default {where}: {why}, not written')
        kept = []
        subtypes = {s['name']: s['cls'] for s in record.get('subobjects', [])}
        for row in rows:
            sub, name, _, _ = row
            if self.transient_default(subtypes.get(sub, path) if sub else path, name):
                self.note(f'{path} default {sub + "." if sub else ""}{name}: transient captured default, not written')
            else:
                kept.append(row)
        rows = kept
        self.default_stats['rows'] += len(rows)
        return rows

    def captured_default(self, path, name):
        """What the game's default object of a class holds for a property (the class's own value, the
        one it changes from a parent, or the nearest parent's), or None when the capture does not say."""
        seen = set()
        while path in self.defaults and path not in seen:
            seen.add(path)
            record = self.defaults[path]
            for key, value in record.get('inherited', {}).items():
                if key.split('.', 1)[1].lower() == name.lower():
                    return value
            for key, value in record.get('own', {}).items():
                if key.lower() == name.lower():
                    return value
            path = record.get('super')
        return None

    def transient_default(self, path, name):
        """A captured member cannot write through a transient containing property."""
        for part in name.split('.'):
            part = re.sub(r'\[\d+\]$', '', part)
            field = next((f for owner in self.db.chain(path) for f in self.db.structures[owner]['fields']
                          if f['name'].lower() == part.lower()), None)
            if field is None:
                return False  # Editor-owned P4Kept_ fields are absent from game reflection.
            if int(field['flags'], 16) & F_TRANSIENT:
                return True
            detail = field['type_detail']
            path = detail.get('struct_path')
            if path is None:
                return False
        return False

    def engine_default_rows(self):
        """P4EngineDefaults.cpp: a row for each asked class and property, and one for each captured
        class below it that changes the value again."""
        rows = []
        for path, name in self.engine_defaults:
            text = literal(self.captured_default(path, name))
            if text is None or text == 'None':
                self.note(f'{path}.{name}: the capture holds no plain default value, no engine default written')
                continue
            rows.append((path, name, text))
            for other, record in sorted(self.defaults.items()):
                above, seen = record.get('super'), set()
                while above and above != path and above in self.defaults and above not in seen:
                    seen.add(above)
                    above = self.defaults[above].get('super')
                changed = next((v for key, v in record.get('inherited', {}).items() if key.split('.', 1)[1].lower() == name.lower()), None)
                if other != path and above == path and changed is not None and literal(changed) is not None:
                    rows.append((other, name, literal(changed)))
        kept = []
        for row in rows:
            if self.transient_default(row[0], row[1]):
                self.note(f'{row[0]}.{row[1]}: transient captured default, no engine default written')
            else:
                kept.append(row)
        rows = kept
        lines = ['// Generated by tools/stubs/gen_headers.py. See P4StubDefaults.h.', '', '#include "P4StubDefaults.h"', '',
                 'const FP4EngineDefault GP4EngineDefaults[] = {']
        lines += [f'\t{{ TEXT("{cpp_text(path)}"), TEXT("{cpp_text(name)}"), {cpp_literal(text)} }},' for path, name, text in rows]
        lines += ['\t{ nullptr, nullptr, nullptr },', '};', '']
        self.default_stats['engine class defaults'] += len(rows)
        return '\n'.join(lines)

    def is_config(self, path, owner, name):
        """True for a property a parent class declares as config."""
        for parent in self.db.chain(path)[1:]:
            if parent.rsplit('.', 1)[1] == owner:
                return any(f['name'] == name and int(f['flags'], 16) & F_CONFIG for f in self.db.structures[parent]['fields'])
        return False

    def subobjects_before(self, path):
        """The default subobjects the stub parent of a class has already made and set, by name."""
        parent = self.db.structures[path]['super_path']
        if parent in self.classes and parent in self.defaults:
            return {s['name']: s for s in self.defaults[parent].get('subobjects', [])}
        return {}

    @staticmethod
    def attached_to_owner(changed, record):
        """True when a subobject's AttachParent is the owning class's own default object."""
        parent = changed.get('SceneComponent.AttachParent') or {}
        return bool(parent.get('object')) and 'sub' not in parent and parent['object'] == record.get('default_object')

    def constructor_from_defaults(self, path, cpp, need):
        """Constructor that makes the default subobjects the game's class default object has and sets
        the game's default values. No lines when there is nothing to do."""
        record = self.defaults[path]
        before = self.subobjects_before(path)
        subs = [s for s in record.get('subobjects', []) if '.' not in s['name']]
        by_name = {s['name']: s for s in subs}
        game_parent = {s['name']: s for s in self.defaults.get(record.get('super') or '', {}).get('subobjects', [])}
        root = (record.get('inherited', {}).get('Actor.RootComponent') or {}).get('sub')
        local = {s['name']: 'Made' + re.sub(r'\W', '_', s['name']) for s in subs}
        lines, initializer, fetched = [], 'ObjectInitializer', set()
        # A component can be listed before the one it attaches to, which is not made yet at its turn: its
        # attachment waits until all are made, or the check on the parent would skip it.
        made, attach_later = set(before), []

        def fetch(sub):
            cls = self.known_class(sub['cls'])
            need(cls)
            kind = self.cpp_name(cls)
            if sub['name'] not in fetched:
                fetched.add(sub['name'])
                lines.append(f'\t{kind}* {local[sub["name"]]} = Cast<{kind}>(GetDefaultSubobjectByName(TEXT("{sub["name"]}")));')
            return kind

        for sub in subs:
            name, var = sub['name'], local[sub['name']]
            changed = sub.get('changed', {})
            attach = (changed.get('SceneComponent.AttachParent') or {}).get('sub')
            # A component's own subobject attached to the component itself: the reference is the class's
            # default object, with no subobject name.
            to_self = self.attached_to_owner(changed, record)
            earlier = before.get(name)
            if name in game_parent and game_parent[name]['cls'] != sub['cls'] and self.known_class(sub['cls']) == sub['cls']:
                need(sub['cls'])
                initializer += f'.SetDefaultSubobjectClass<{self.cpp_name(sub["cls"])}>(TEXT("{name}"))'
            earlier_changed = (earlier or {}).get('changed', {})
            attach_before = (earlier_changed.get('SceneComponent.AttachParent') or {}).get('sub')
            parent_record = self.defaults.get(self.db.structures[path]['super_path'], {})
            if earlier and attach == attach_before and to_self == self.attached_to_owner(earlier_changed, parent_record):
                continue  # the stub parent made it and placed it the same way
            kind = fetch(sub)
            if attach in by_name:
                fetch(by_name[attach])
            socket = (changed.get('SceneComponent.AttachSocketName') or {}).get('name')
            socket = f', FName(TEXT("{cpp_text(socket)}"))' if socket and socket != 'None' else ''
            if not earlier:
                # A parent class that is not a stub may already have made it.
                lines += [f'\tif (!{var})', '\t{', f'\t\t{var} = {self.make_subobject(path, self.known_class(sub["cls"]), kind, name)};', '\t}']
            made.add(name)
            if attach in by_name and name != root:
                attached = [f'\tif ({var} && {local[attach]})', '\t{', f'\t\t{var}->SetupAttachment({local[attach]}{socket});', '\t}']
                if attach in made:
                    lines += attached
                else:
                    attach_later += attached
            elif to_self:
                lines += [f'\tif ({var})', '\t{', f'\t\t{var}->SetupAttachment(this{socket});', '\t}']
        lines += attach_later
        if root in by_name:
            fetch(by_name[root])
            lines += [f'\tif ({local[root]})', '\t{', f'\t\tRootComponent = {local[root]};', '\t}']
        rows = self.default_rows(path)
        if not lines and not rows and initializer == 'ObjectInitializer':
            return []
        out = []
        if rows:
            out.append(f'static const FP4StubDefault {cpp}Defaults[] = {{')
            for sub, name, value, kind in rows:
                line = f'\t{{ TEXT("{cpp_text(sub)}"), TEXT("{cpp_text(name)}"), {cpp_literal(value)}, FP4StubDefault::{kind} }},'
                out += ['#if WITH_EDITORONLY_DATA', line, '#endif'] if name.startswith(KEPT) else [line]
            out += ['};', '']
        out += [f'{cpp}::{cpp}(const FObjectInitializer& ObjectInitializer) : Super({initializer})', '{'] + lines
        self.default_table += [[self.editor_path(path), *row] for row in rows]
        if rows:
            short = path.rsplit('.', 1)[1]
            out.append(f'\tFP4StubDefaults::Apply(this, TEXT("{short}"), {cpp}Defaults, UE_ARRAY_COUNT({cpp}Defaults));')
        self.default_stats['classes with a constructor'] += 1
        return out + ['}', '']

    def enum_storage(self, path):
        """The integer type an enum class is declared with: what the game stores it in, or the
        smallest type that holds every entry when that one does not (a property tag stores an enum
        value by name, so the width does not change what is cooked)."""
        width = self.enum_width.get(path, 'uint8')
        # Not the <Enum>_MAX entry UnrealHeaderTool adds itself: it is one past the largest value,
        # which is 256 for a byte enum that uses 255.
        last = path.rsplit('.', 1)[1] + '_MAX'
        numbers = [v['value'] for v in self.db.enums[path]['values'] if v['name'].split('::')[-1] != last]
        fits = lambda kind: not numbers or (RANGE[kind][0] <= min(numbers) and max(numbers) <= RANGE[kind][1])  # noqa: E731
        return width if fits(width) else next((kind for kind in RANGE if fits(kind)), 'int64')

    def enum_text(self, path):
        record = self.db.enums[path]
        name = path.rsplit('.', 1)[1]
        form = self.enum_form(path)
        width = self.enum_storage(path)
        values, seen = [], set()
        for value in record['values']:
            short = value['name'].split('::')[-1]
            if short == name + '_MAX':
                continue  # UnrealHeaderTool adds this one itself
            if short in seen or not IDENT.match(short) or short in KEYWORDS:
                self.note(f'{path}: enumerator {short} cannot be declared, skipped')
                continue
            seen.add(short)
            values.append((short, value['value']))
        undef = [f'#undef {short}' for short, _ in values if short in MACROS]
        for line in undef:
            self.note(f'{path}: {line[7:]} is a macro name, #undef written before the enum')
        rows = [f'\t{short} = {number},' for short, number in values] or ['\tNone = 0,']
        if values and not any(number == 0 for _, number in values):
            # UnrealHeaderTool wants an entry for 0, the value a new property starts with. Hidden from
            # the editor's lists; the game's enum has no such entry.
            zero_name = 'P4StubZero' if form != 'plain' else name + '_P4StubZero'
            rows.insert(0, f'\t{zero_name} = 0 UMETA(Hidden),')
            self.note(f'{path} has no entry for 0: a hidden one ({zero_name}) is declared')
        if form == 'class':
            head = [f'UENUM({"BlueprintType" if width == "uint8" else ""})', f'enum class {name} : {width}', '{']
            return undef + head + rows + ['};', '']
        if form == 'namespace':
            return undef + ['UENUM(BlueprintType)', f'namespace {name}', '{', '\tenum Type', '\t{'] + \
                ['\t' + row for row in rows] + ['\t};', '}', '']
        return undef + ['UENUM(BlueprintType)', f'enum {name}', '{'] + rows + ['};', '']


def zero(value):
    """True for a value a zeroed object already has."""
    if isinstance(value, dict):
        return value in ({'array': []}, {'name': 'None'}, {'str': ''}) or ('enum' in value and value.get('value') == 0) or \
            (value.get('count') == 0 and 'unread' not in value)
    return value is None or value is False or (not isinstance(value, bool) and value == 0)


def same(a, b):
    """Two captured values, with references to an owner's own objects compared by their place under it."""
    if isinstance(a, dict) and isinstance(b, dict):
        if 'sub' in a or 'sub' in b:
            return a.get('sub') == b.get('sub')
        return a.keys() == b.keys() and all(same(a[k], b[k]) for k in a)
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    return a == b and type(a) is type(b)


def quoted(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\r', '\\r') + '"'


def literal(value):
    """Unreal's text for a value inside a struct or an array. None when it cannot be written as text."""
    if value is None:
        return 'None'
    if isinstance(value, bool):
        return 'True' if value else 'False'
    if isinstance(value, float):
        return '%.9g' % value
    if isinstance(value, int):
        return str(value)
    if not isinstance(value, dict):
        return None  # "nan", "inf"
    if 'name' in value:
        return quoted(value['name'])
    if 'str' in value:
        return quoted(value['str'])
    if 'enum' in value:
        return value['entry'].split('::')[-1] if 'entry' in value else str(value['value'])
    if 'soft' in value:
        return quoted(value['soft'])
    if value.get('struct') in ('SoftObjectPath', 'SoftClassPath') and 'AssetPathName' in value.get('fields', {}):
        # Unreal reads and writes these two as a path, not member by member.
        asset = value['fields']['AssetPathName'].get('name', 'None')
        sub = (value['fields'].get('SubPathString') or {}).get('str', '')
        return 'None' if asset == 'None' else quoted(asset + (':' + sub if sub else ''))
    if 'fields' in value:
        parts = []
        for name, inner in value['fields'].items():
            text = literal(inner)
            if text is None and isinstance(inner, dict) and ('unread' in inner or 'count' in inner):
                continue  # text, delegates, maps: the member keeps what the struct starts with
            if text is None:
                return None
            parts.append(f'{name}={text}')
        return '(' + ','.join(parts) + ')'
    if 'array' in value:
        if not value['array']:
            # Unreal reads "()" as an array of one default element. No text is its empty array, on its
            # own and after "Member=" inside a struct.
            return ''
        parts = [literal(inner) for inner in value['array']]
        return None if None in parts else '(' + ','.join(parts) + ')'
    return None  # object references, fixed arrays in structs, maps, sets, unread kinds


def leaves(path, value, sub, rows, skipped):
    """Rows for one property value. Each struct member and fixed-array element is a row of its own,
    so a member the editor's version of a struct lacks costs only that member."""
    where = (sub + '.' if sub else '') + path
    if isinstance(value, dict):
        if 'fields' in value:
            for name, inner in value['fields'].items():
                leaves(path + '.' + name, inner, sub, rows, skipped)
            return
        if 'dim' in value:
            for index, inner in enumerate(value['dim']):
                leaves(f'{path}[{index}]', inner, sub, rows, skipped)
            return
        if 'object' in value:
            if 'sub' in value:
                rows.append((sub, path, value['sub'], 'Subobject'))
            elif value['object'].startswith('/Script/') and not value.get('unknown'):
                rows.append((sub, path, value['object'], 'Object'))
            else:
                skipped.append((where, 'a reference to an asset / ' + value['object']))
            return
        if 'count' in value:
            if value['count']:
                skipped.append((where, 'a map, a set or a long array / {} entries'.format(value['count'])))
            return
        if 'unread' in value:
            return  # text and delegates: nothing was read
        for kind in ('name', 'str', 'soft'):
            if kind in value:
                rows.append((sub, path, value[kind], 'Text'))  # the whole text is the value: no quotes
                return
    text = literal(value)
    if text is None:
        skipped.append((where, 'an array that holds references' if isinstance(value, dict) else 'not a finite number'))
    else:
        rows.append((sub, path, text, 'Text'))


def cpp_text(text):
    """The inside of a C++ wide string literal."""
    out = []
    for character in text:
        code = ord(character)
        if character in '\\"':
            out.append('\\' + character)
        elif character in '\n\r\t':
            out.append({'\n': '\\n', '\r': '\\r', '\t': '\\t'}[character])
        elif code < 32 or code == 127:
            out.append('?')
        elif code > 126:
            out.append('\\u%04x' % code if code < 0x10000 else '\\U%08x' % code)
        else:
            out.append(character)
    return ''.join(out)


def cpp_literal(text, piece=2000):
    """TEXT("...") for any length: the compiler limits one literal, and joins adjacent ones."""
    parts = [text[at:at + piece] for at in range(0, len(text), piece)] or ['']
    return ' '.join(f'TEXT("{cpp_text(part)}")' for part in parts)


def reflected(text):
    """UnrealHeaderTool only writes a .generated.h for a header that declares something reflected."""
    if re.search(r'^(UENUM|USTRUCT|UCLASS|UINTERFACE|DECLARE_DYNAMIC)', text, re.M):
        return text
    return re.sub(r'#include "\w+\.generated\.h"\n', '', text)


HEADER = '''// {module} {what}: generated by tools/stubs/gen_headers.py from the reflection capture.
// Names, parents and property types of the game's classes, no behavior. Do not edit.
#pragma once
'''

BUILD = '''using UnrealBuildTool;

public class {module} : ModuleRules
{{
	public {module}(ReadOnlyTargetRules Target) : base(Target)
	{{
		PCHUsage = PCHUsageMode.NoPCHs;
		PublicDependencyModuleNames.AddRange(new string[] {{ {deps} }});
	}}
}}
'''


def write(files, out):
    out = Path(out)
    for relative, text in files.items():
        path = out / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.is_file() and path.read_bytes() == text.encode('utf-8'):
            continue  # an unchanged file keeps its time, so the next build does not compile it again
        path.write_text(text, encoding='utf-8', newline='\n')


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument('classes', nargs='*', help='class paths, e.g. /Script/Carrot4.GActor')
    parser.add_argument('--coverage', help='coverage.py output: every class the map uses')
    parser.add_argument('--capture', default=str(reflection.CAPTURE))
    parser.add_argument('--engine-index')
    parser.add_argument('--subobjects')
    parser.add_argument('--defaults', help="eden_defaults.py's defaults.json: the game's default values")
    parser.add_argument('--closure', choices=('minimal', 'full'), default='minimal')
    parser.add_argument('--expose', choices=('game', 'all'), default='game')
    parser.add_argument('--functions', action='store_true', help='also declare the reflected functions')
    parser.add_argument('--engine-declared', help="what the launcher's editor declares, to find the game engine's additions")
    parser.add_argument('--zero-constructor', help='JSON list of struct paths the game makes by zero-filling')
    parser.add_argument('--engine-default', action='append', default=[], metavar='CLASS:PROPERTY',
                        help="a default of an engine class that the game's engine has otherwise (the value is the capture's)")
    parser.add_argument('--runtime', help="UE4 recomp bridge: class_plan.py's class-plan.json; game layouts and original code")
    parser.add_argument('--out', required=True)
    args = parser.parse_args(argv)
    roots = list(args.classes)
    if args.coverage:
        coverage = json.loads(Path(args.coverage).read_text(encoding='utf-8'))
        roots += [c for row in coverage['classes'].values() for c in row['chain']]
    load = lambda p: json.loads(Path(p).read_text(encoding='utf-8')) if p else None  # noqa: E731
    generator = Generator(reflection.Database.load(args.capture), load(args.engine_index), load(args.subobjects), args.expose,
                          defaults=load(args.defaults), functions=args.functions,
                          engine_declared=load(args.engine_declared), zero_constructor=load(args.zero_constructor),
                          engine_defaults=[tuple(item.rsplit(':', 1)) for item in args.engine_default],
                          runtime=load(args.runtime))
    generator.select(roots, args.closure == 'full')
    files = generator.files()
    write(files, args.out)
    Path(args.out, 'generator-notes.txt').write_text('\n'.join(generator.notes) + '\n', encoding='utf-8')
    print(f'{len(generator.classes)} classes, {len(generator.structs)} structs, {len(generator.enums)} enums, '
          f'{sum(1 for v in generator.delegates.values() if v[1])} delegates, {len(generator.notes)} notes, '
          f'{len(files)} files in {args.out}')
    for what, count in sorted(generator.default_stats.items()):
        print(f'defaults: {count} {what}')
    for what, count in sorted(generator.function_stats.items()):
        print(f'functions: {count} {what}')


if __name__ == '__main__':
    main(sys.argv[1:])
