# Native Blueprint planning boundary

`blueprint_native_plan.h` is a portable C++14 planner over an explicitly supplied
cooked package header/body and logical package name. It composes bounded class,
FProperty, UFunction, Kismet, stored-value, graph, sequencer and animation readers.
The released caller supplies native reflection from its qualified loaded Unreal
declarations. Python tools are independent test oracles, not runtime dependencies.

```cpp
using namespace P4MapKit::Blueprint;
NativeBlueprintPlan cached = BuildNativeBlueprintPlan(
    header, body, logicalPackage, signatures, true);
cached.RequireSource(currentHeader, currentBody);
const std::string& importJson = cached.ImportText();
```

The result holds immutable canonical `p4-blueprint-plan` / `p4-blueprint-evidence`
version 1 texts and separate SHA-256 header/body digests. `RequireSource` refuses
changed source bytes with `native-plan-source-changed`. Texts contain logical
asset names and Unreal import text; the file resolver and its machine paths are
caller inputs and never plan members. The result is a planning artifact. Editor
creation, compilation, cooking and behavior validation are separate boundaries.

`BuildBlueprintPlanDocument(inventory, body, signatures, withGraphs)` returns the
combined native plan. `NativeBlueprintPlanIndex(map<string, J>)` emits the v1
index's dependency-ordered `order`, `with_graphs` and `needs` members. The caller
can instead pass `map<string, NativeBlueprintPlan>` directly. Each cached plan's
const `IndexEntry()` exposes a small typed index projection without reparsing
canonical text or retaining a duplicate full plan document. The caller
retains the original module/package identities and verifies every selected
source and dependency before consuming a cache. It also invalidates caches when
its qualified native declaration snapshot or planner version changes.

`AdmissionIssues()` exposes nonempty planner diagnostics before editor creation:
top-level skipped graphs, unread fields, problems, skipped variables, unplaced
components and unhandled class fields, plus exact nested evidence triples for
`unread`, `problem` and `notes`. It holds no complete graph or stored-value
document. The editor applies its qualified admission policy to these named issues;
the cache does not silently approve partial authoring.

`GraphSignatures` accepts explicit reflection and a native package loader:

* `functions`: exact `path`, hexadecimal `flags`, `script_signature.parameters`
  in declaration order (`name`, `is_return`, `is_out`, `is_reference`) and optional
  nested `script_signature.return_property` (`name`, `type_detail.enum_path` when
  actually established).
* `structures`: exact `path`, `super_path` or null, ordered `fields` containing
  `name` and `type_detail`. Animation enum recovery consumes `enum_path`.
* `enums`: exact `path` and ordered `values` containing `name`.

An absent native function remains absent; its graph entry reports an unsupported
signature. A loaded declaration adapter must omit unsupported definitions rather
than invent parameters, flags, ownership or enum identity. Empty parameters are
appropriate only for an established zero-parameter declaration. Reverse-engineering
captures are private comparison inputs and are not a product dependency.

For Blueprint signature dependencies, `ReadClassStructure(inventory, body)`
provides class and function declarations without decoding stored object values.
The signature cache does not need object values or class values. The main planner
keeps the full value document within its request for components, animation and
sequencer recovery.

All readers enforce byte spans, count/depth/node limits and supported native
layouts. Class/script envelope failures refuse a plan; unsupported tagged values
remain named `unread` evidence. The authoring value reader never guesses a Guid
layout from a failed struct-map parse. `DeepValues`' explicit comparison mode
exists solely to compare the historical private reader and is not used by this
facade. Refusal diagnostics for unsupported compiler fields may differ from that
historical reader without changing admitted values.

The standalone `native_blueprint_*` tools exercise each boundary independently,
the complete canonical import plan, changed-source refusal and sanitizers. The
original corpus tests read their explicit source indexes and write scoped receipts;
they do not copy or export the game's source packages.
