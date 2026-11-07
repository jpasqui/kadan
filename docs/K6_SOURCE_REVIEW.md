# K6 portable source review

The reviewed snapshot contains 212 pending mapkit files. [The individual inventory](K6_SOURCE_REVIEW.json) records each path, byte hash, classification, includes, and review findings. Its sorted path/hash fingerprint is `7c744add144d5bdeeca153cb9ac2221399612c02f69cb16cb211ddced1c96ca7`.

This is a historical review, not a current release qualification. Receipt locations are omitted from the publication inventory. The file identities, classifications, measured results, and admission limits are retained.

| Classification | Files | Result |
| --- | ---: | --- |
| Qualified host source | 2 | Publish bounded readers with focused validators. Neither is admitted to production. |
| Reusable source candidate | 12 | Keep out until the owning row supplies current qualification. |
| Incomplete research headers | 130 | Keep canonical research in place and outside the plugin. |
| Tests and probes | 61 | Host-only executables. Never module or archive members. |
| Research notes | 7 | Retain as research. Never plugin members. |

## Published source and measured scope

`navigation_boundary.h` is a bounded serialized navigation-tail reader. The retained review receipt binds the reader, test and driver hashes. The recorded host run read the retained payloads in place. It matched 8, 9 and 9 serialized tiles, exercised synthetic wire sections, and refused a non-finite segment. This does not prove a navigation rebuild or walking in the game.

`material_graph.h` validates graph-file schema. The retained review receipt binds its narrow dependency and validator. The recorded fatal AddressSanitizer/UndefinedBehaviorSanitizer run passed six semantic checks and pinned the reviewed bytes. Its tracked `material_json.h` dependency differed from the retained receipt only through LF/CRLF checkout conversion. The qualification receipt recorded that distinction. This does not qualify the larger material compiler or rendered output. The wider `native_material_boundary.cpp` probe depends on unfinished material-stage research and is not published with this row.

The retained receipts are separate from plugin source and archive members. Their locations and local input paths are not part of this public review. Release qualification must bind the source and archive being distributed; this historical review cannot substitute for that check.

## Production admission and source ownership

At the time of the review, production consumed 44 canonical core files and 12 explicitly listed first-party Maxwell files. Their 56 admission records described existing product dependencies, not new game qualification. Staging does not invoke Git, so the same policy works on a remote mirror without a `.git` directory.

`native_core_sources.py` resolves canonical authoring and Blueprint module headers and non-automation translation units, including UHT headers and the PCH. It walks internal literal includes conservatively across conditionals. It uses the including directory, the module's Public/Private roots, dependency Public roots, and explicit `P4MapKitCore`/`KadanMaxwell` mappings. It refuses ambiguous, escaping, macro, missing internal or unlisted portable dependencies. Remaining engine, generated and standard-library headers are external inputs.

`native_core_files.txt` lists the portable files the plugin compiles, one repository path a line. A file the plugin's sources reach that is not listed stops staging; a listed file the plugin does not reach is not copied. Compiler provenance and ownership receipts keep raw byte hashes.

Seven candidates were reserved for qualification: `creator_mesh_package.h`, `mesh_cooked_readback.h`, `physics_body.h`, `static_mesh_facts.h`, `mesh_nav_collision.h`, `export_package.h`, and `mesh_collision_fidelity.h`. Their tracked dependency closure was `package_reader.h`, `quality.h`, `bulk_data.h`, `static_mesh_render.h`, and `property_decoder.h`. The additive `asset_extension.h` and `wwise_opus_wem.h` use the same explicit promotion route. This review does not claim those files or their later qualification.

The compiler uses the same fixed seven sources, five headers and portable bits helper as the existing build. Its bounded adaptations and relative provenance remain intact. Offline backend fixture builds retain their research staging API.

A `core-module-sources.json` ownership receipt permits removal or replacement only while the staged bytes match their recorded hashes. The complete source plan and destination are checked before mutation. Unknown or modified files survive refusal. A stage with no receipt may adopt only exact planned bytes. An old blanket stage containing excluded research refuses and requires its owner to review that snapshot or choose a fresh isolated stage. No canonical research is removed. Binary and Intermediate folders are untouched.

Development mode can admit a reviewed dependency with `scope: tests` before its product qualification, including a dependency used while the owning lane proves a product helper. Release staging refuses that scope. Development automation stays in its separate module. Release source labeling verifies owned bytes before adding diagnostic directives, then updates the hashes for those exact transforms and compiler provenance. Repeated staging and labeling avoid unchanged rewrites.

## Validation

Run `python tools/test_native_core_sources.py --review-root <original-checkout>`. Its focused suite checks transitive closure, exact admission, conditional includes, ambiguity and escape refusal, owned removal, preservation of foreign changes, legacy adoption, receipt validation, link metadata refusal, automation isolation, compiler adaptations, and label compatibility. Synthetic repositories have no Git directory. Link metadata cases are injected into the staging guard.

The historical navigation validator used three retained `--payload` inputs, and the graph validator used `--sanitizers`. Their recorded results are host evidence. No Unreal build, cook, game boot, plugin archive, or clean-project acceptance is claimed by this row.
