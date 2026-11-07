# retoc for Kadan

Kadan uses retoc 0.1.5 (https://github.com/trumank/retoc, MIT licence, commit 885a8da) with the changes in
`kadan-retoc.patch`. They make `to-zen` pack a game package the way UE 4.26's own IoStore utility packed the game, so
an exported game package is byte for byte the game's:

- With `--version UE4_26`, a package's exports stay in one export bundle. UE 4.26 starts a new bundle only where the
  load order moves to another package (`BuildBundles` in `IoStoreUtilities.cpp`); unpatched retoc closes one after each
  public export, as later engines do, which added 8 bytes per extra bundle.
- `to-zen --reference <utoc or folder>`: a UE4 dependency arc to a package that is not in the container being made, but
  is in the reference containers (the game's, for a mod), names the export bundle that package's own header gives the
  export. Unpatched retoc writes -1 ("the last bundle"). Kadan's export passes the game's Paks folder.
- UE4 graph data keeps each external arc once per imported package, in the order it first came, as UE 4.26's
  `AddUniqueExternalBundleArc` does. Unpatched retoc tells arcs apart by their fix-up placeholders, so arcs that are
  the same after the fix-up were all written. The imported packages are written in the order of their ids, as every
  graph in the game's containers has them; unpatched retoc writes them in the order it first meets them.

Without `--reference` and for other engine versions nothing changes. `to-legacy` (the import's conversion) gives
identical files.

To build: apply the patch to retoc 0.1.5 at 885a8da and run `cargo build --release -p retoc_cli`. The built `retoc.exe`
goes in `private/retoc/` next to retoc's `LICENSE`, where the release and the tests take it from. The unpatched one is
kept there as `retoc-0.1.5-upstream.exe`.
