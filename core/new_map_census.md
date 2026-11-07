# Read-only map allocation census

`P4MapKit::MapCensus::Read(Input)` projects the final Tsuchi `records.cpp` and
`new_maps.cpp` allocation schema from exact file bytes. It opens no files,
allocates no game slots, migrates nothing, and writes nothing. `Input` has
`First`, `Second`, and `Legacy` files; each carries `Presence`, exact `Bytes`,
and a diagnostic `Digest`. Caller digest strings do not establish validity.

The native driver acquires these fixed paths under its actual save-root custody:

- `save:/tsuchi/tsuchi.content/new_maps.json`
- `save:/tsuchi/tsuchi.content/new_maps.2.json`
- `save:/tsuchi/new_maps.json` (legacy)

The native wrapper computes SHA-256 from acquired bytes, holds the appropriate
read/custody handles, and compares fresh snapshots before publication. The
portable `Result::Snapshot` retains every supplied byte/digest, including unknown
members, unused copies, and legacy input. No serialized or reconstructed JSON
can stand in for that acquisition. Unknown fields and non-free-area entries are
not modified or dropped from these snapshots.

`counter` is optional (zero when absent); when present it must be a lexical whole
signed 64-bit integer from zero. The required `areas` object can be explicitly
empty. For known free areas 5, 7, 8 and 9, entries require joined `map` IDs and
boolean `used`; duplicate area aliases or map IDs invalidate the copy. Numeric
keys with leading zeros match the runtime. Unknown-only roots lack `areas` and
are invalid; an invalid newest copy can therefore fall back to an older valid
allocation rather than allowing IDs to be reused.

Both canonical copies are inspected, with the highest semantically valid counter
selected. Equal generations use the runtime's first-copy preference. For export,
different retained allocations at an equal counter additionally produce
`Ambiguous`, which refuses context construction. Legacy is inspected only when
both canonical copies are absent. A present damaged canonical pair cannot fall
back to legacy. A read failure blocks acquisition rather than selecting another
copy. `used:false` entries are still occupied: this authoring census does not
implement the runtime's conditional unused-slot reclamation.

`IsUsable` accepts `Empty`, `Valid`, `Recovered` (a canonical valid-copy
fallback), and `Legacy`. It rejects `Damaged`, `Unavailable`, and `Ambiguous`.
The driver must check the status, not treat an empty `Occupied` vector as proof
of a free save. A damaged pair never authorizes allocation. It must also prove
the target game/table inventory before combining `Occupied` with the definition
context. A recipe or manifest count does not prove occupancy or free areas.

`kMaxRecordBytes` is 2 MiB per copy, an explicit authoring acquisition bound.
Oversize input reports `Unavailable`. It is not a runtime size limit: the final
runtime's `read_whole_file` allocates the measured file size. The parser also
has 100,000-node and 64-depth limits. UTF-8, duplicate keys and numeric grammar
are checked; unknown numeric values use the runtime's finite scaling behavior,
including finite underflow to zero.

`tests/new_map_census_test.cpp` covers newest/older recovery, corruption refusal,
legacy precedence, equal-counter ambiguity, unknown-data retention, unused
claims, numeric boundaries, duplicate aliases/IDs, read failure, and acquisition
bounds. The combined portable runner is
`tools/navigation/test_authoring_definitions.py`.
