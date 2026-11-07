# Native map kit core

This is the engine-independent C++14 admission layer for the kit rewrite.
`core/` is canonical. The authoring build tools stage its headers into
the UE plugin; hand-editing a staged copy does not update the core.

`ParseInteger` preserves signed and unsigned 64-bit values without a JSON double
or an out-of-range signed conversion. `CompleteSpans` rejects gaps, overlaps and
overflowing source ranges. The import audit, the job state machine and the import
plan (`AuditImport`, `FImportJob`, `core/import_plan.h`) were removed with the older
import chain on 2026-10-10: nothing else used them.

`core/package_reader.h` inventories retoc version-zero legacy UE4.26 package
directories and top-level property tag envelopes in C++. It checks file/table
bounds, reference cycles, payload overlap, signed counts, Unicode strings and tag
flags. Every header/body byte has a span; uninterpreted regions remain explicit.
`ReadPackageHeader` produces the same directory census using the companion file's
actual length without allocating its payload. It checks signed export bounds even
for large bodies; the full `ReadPackage` entry point retains its 64 MiB input cap.
Neither entry point qualifies the payload for editable import.
The editor's `NativePackageInventory` accepts separate immutable header/body files
so the original import preflight reads the custody snapshot rather than a changing
extraction cache. Directory validity never sets `editable_import_qualified` true.

Source-custody verification also runs in C++ on Win64. It validates the manifest,
ordered header/body and optional bulk records, checks every size and SHA-256,
and checks the combined header/body hash with Windows CNG. Reads use a fixed
64 KiB buffer within the current 32 MiB total custody limit; larger inputs refuse
until the full large-asset adapter is qualified. JSON depth and record shape are
bounded before file reads. Normal admission uses this verifier before and after
decoding and before publication, with no Python verification fallback.

Private capture/publication also runs in C++. It holds source read handles,
matches the caller's parser snapshot, captures both optional bulk sidecars, and
reuses verified content-addressed blobs. Missing blobs are streamed into exclusive
temporary files, flushed and atomically hard-linked without replacement. A
conflicting/corrupt preserved file is never overwritten. Content destinations
and directory junctions are refused. DataTable field/reference admission now
uses actual C++ FProperties, including nested maps/arrays and complete fixed
slots. Supported scene/count/color asset schema and evidence admission now also
use C++. Broader orchestration still needs porting; the snapshot hash bridges the
transitional inspector to native capture.

`NativeCanonicalTableRows` resolves native field identities, groups serialized
fixed slots, validates supported exact values and checks hard-reference targets
against native PropertyClass/MetaClass. Duplicate JSON fields, native field/row
identity collisions, incomplete slots, unknown fields, excessive nesting, engine
name overflow and escaped NULs refuse before table mutation. Numeric transport
retains original tokens: UE4's generic serializer otherwise rounds uint64 IDs.
Soft paths keep the engine's lazy-reference semantics. Original collection
create/reimport/restart checks pass with Python schema/reference checks disabled.
`NativeTableReadback` independently reads the actual staged table and compares
every serialized override using FProperty types. Fixed/dynamic arrays, nested
structs and typed maps retain their lengths, key identities and exact values;
map storage order and additional native defaults are outside the serialized
override. Scalar projection preserves all integer bits, floating signed zero,
FName/enum identities and complete supported FText runtime metadata. The native
importer and group job refuse a readback mismatch before live row publication.
Normal original imports pass with Python comparison/projection disabled; private
Python oracles still independently check results. `NativeAdmitTable` independently
decodes the preserved package, binds its package and row type, and compares the
complete serialized row inventory with both the caller's input and actual staged
table. Omitting fields/rows or substituting fabricated source values cannot qualify
as original data. Native Windows CNG hashes exact UTF-8 typed projections and
assembles/audits the evidence; normal imports no longer use script evidence hashing.
The versioned digest convention is `p4-native-typed-json-v1`. Authored changes are
applied after original-source admission and survive reimport. Broader orchestration
and other asset families still need porting. This is not
full native delivery or complete default/asset recovery.

`NativeCanonicalAssetValues`, `NativeAssetReferences` and `NativeAssetReadback`
use the same typed native admission/comparison as tables. Reference discovery can
inventory unresolved targets; actual writes still require PropertyClass/MetaClass
compatibility. `NativeApplyAssetValues` writes supported properties directly on a
transient copy, verifies independent readback, then publishes one undo transaction
and checks the result. Native maps replace/clear correctly, nested partial edits
retain other fields, and integers retain all 64 bits. Invalid data leaves the
target intact; GC guards retain staging/backup objects during callbacks.

`NativeAdmitAsset` shares original-package binding and native evidence assembly
with table admission. `NativeValidateAssetSource` validates original serialized
inventory without loading dependencies or comparing current authored values.
An unchanged-source reimport uses that check and preserves authored edits and
intentional dependency deletions; it does not recreate stock assets. New imports
still require full staged readback admission before creating their target asset.
The script continues resolving/saving dependency closures; whole new/mixed graph
publication and filesystem/callback rollback remain unqualified.

`core/property_decoder.h` now decodes original table rows and scene/count/color tagged
values. Integer JSON tokens retain all 64 bits; finite floats retain their exact
serialized values. Nested tagged structs, supported native structs, arrays/sets,
map pairs and references require exact payload consumption. Unsupported text
histories, native structs, GUID payloads and collection removals refuse. Depth,
node and text budgets bound decoding. The established `Property`, `Properties`,
`Document` and `TableRows` entry points retain their original interchange and
refusals, including refusing soft subobject paths. The normal original import
entry point uses `NativeDecodeOriginalPackage` on the immutable source pair,
without an inspection-decoder fallback. Private source custody, recovered schema
admission and independent UE staged readback still follow; decoding values alone
does not qualify complete editable import.

`TypedProperty`, `TypedProperties`, `TypedDocument` and `TypedTableRows` expose
distinct name, enum, hard-reference, soft-path, field-path and native-text kinds.
Names preserve their archive index, base spelling and number; enums retain their
declared enum and whether their value is a byte ordinal or an FName. References
retain their package index, including null. A string that spells an asset path
remains a string. Soft paths preserve asset and subobject parts separately. The
typed path additionally understands class/weak/class-soft references and modern
field paths with at most 64 property-outward names and a checked owner. It does
not resolve fields or load dependencies. Base/None FText histories preserve the
complete existing `__native_text` metadata; other histories still refuse.

Typed entry points publish no partial output or cursor on failure. `DecodedJson`
continues projecting the established importer shapes, and `LegacyDecodedValue`
explicitly erases type identity for existing consumers. Parsing that JSON does
not recover the erased archive identity. Portable fixture checks and the corpus
probe compare typed, erased and original projections independently; they do not
qualify additional asset families for editable import.

Struct-valued maps require codecs from the recovered owning UE schema: map
archives do not encode those struct names. Native-serializer flags prevent
unknown native layouts being guessed as tagged records. Struct arrays now handle
soft object/class paths and supported gameplay tag forms with exact consumption.

Independent comparisons pass on all 1,530 cached original table/scene/count/color
packages (1,050,092 leaf values), including struct-valued maps, soft-path arrays
and localized text. Matching values
does not prove all recovered UE types/dependencies exist. Native export tails,
remaining summary/preload/registry sections, bulk data, versioned stock headers,
direct containers and writers remain open.

Actual UE table import/readback now handles typed map pairs and complete fixed
arrays. Fixed source tags such as `Num`, `Num[1]`, `Num[2]` become the declared
three-slot property without dropping slots or inventing defaults. Sparse fixed
arrays still refuse. Map keys use their native identity, FString keys retain
case, map readback order is deterministic, and numeric/type/range admission
precedes live mutation. Float readback retains floating tokens and negative zero.
Five original collection tables pass import, authored map values/custom rows,
map deletion/reimport and separate restart checks in an owned clean project.
The original Onyon color asset imports as its actual native UDataAsset class;
all eight serialized colors match exactly. Edits and a custom asset survive save,
separate restart and reimport. Unknown fields and native payload tails refuse.

Localized `FText` retains its flags, history, namespace, key and source rather
than just its display string. Base and None histories have exact bounded codecs;
other histories refuse. Editor admission supports qualified localized, invariant
and empty values, verifies UE's parsed literal against the native metadata, and
reports runtime namespace identity without UE's editor package suffix. Original
keys and authored edits/deletions survive save/restart and actual cooking of the
two owned text DataTable fixtures. This does not qualify UI reconstruction,
string tables, other text histories or a full mod export.

The authoring plugin now has a native ticker-driven table job. It owns staged
tables/snapshots through UE garbage collection, exposes progress/cancellation,
detects intervening edits and publishes rows plus optional baseline metadata in
one undo transaction. Publication failure restores original rows and metadata,
transaction flags and package dirty state. Terminal jobs release asset references;
abandoned jobs cannot publish; module shutdown cancels outstanding callbacks.
Mutations stay on the game thread. Original-table adapters consume this job, but
their transitional Python front end drives its stages synchronously.

Reimport row planning also runs in C++. Qualified source and baseline tables
use actual FName row identities; native row copies keep current authored values,
custom rows and deliberate deletions. Only newly discovered original rows are
added. Planning leaves all inputs intact and produces exact native row/baseline
readback for the publication job. The Python merge is only a test oracle in this
path; schema admission and surrounding workflow are still transitional.

This is row/metadata publication for existing table objects, not an atomic
whole-project filesystem import. Remaining package formats, background CPU work,
new asset creation/dependency graphs, save rollback and interactive progress UI
qualification remain part of the rewrite. Engine table callbacks can run during
publication; the job checks final readback and refuses unfaithful results, but
does not claim isolation of arbitrary callback side effects.

Run `tools/navigation/native_core_build.py` from the repository with the configured
Python build runtime. Python invokes compiled C++ tests, Clang coverage, static
analysis and address/undefined-behavior sanitizer builds. Assertions and production
logic execute in C++. Undefined behavior is fatal, and sanitizer output must agree
with the unsanitized test result. Output stays under the owned private quality folder.

The runner inventories all handwritten core sources, including ones that did
not appear in instrumentation, and reports line and branch coverage against the
90% targets. Coverage does not decide a release; the acceptance chain does.
`native_quality_policy.json` separately inventories the editor and owned runtime
sources. Core coverage never counts as measured editor/runtime coverage.

`native_release_publish.py` accepts only exact qualified native archives. It
checks current source hashes, all required evidence and the archive hash, audits
members, stages the new file and rechecks before atomic replacement. Rejection
preserves the previous release. Native editor actions and remaining processing
ports still need qualification before that gate can pass.

Native UE automation is compiled only by an explicit candidate build with
`--automation-tests`; normal production builds omit it. The native publisher
also rejects an authoring DLL carrying those test registrations.
