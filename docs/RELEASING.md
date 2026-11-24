# Releases

A release is built on the desktop, which has Unreal Engine 4.26, and uploaded to GitHub by hand.
One command on the laptop checks the checkout, sends it to the desktop, builds there and brings the
archive back. No workflow builds or uploads a release.

## On the desktop, once

The release build reads these from the desktop's mirror of the repository
([Remote Unreal](REMOTE_UNREAL.md) sets up the connection):

- Unreal Engine 4.26 at `P4_UE_ROOT` (default `C:/UE_4.26`), the engine Kadan is built for.
- `private/retoc/` with `retoc.exe` and its `LICENSE`, and `private/repak/` with `repak.exe`,
  `LICENSE-MIT` and `LICENSE-APACHE`. Each executable must have the SHA-256 and size pinned in
  [`tools/release_tools.json`](../tools/release_tools.json), which also says what each is built from.
  To ship another build of either, change its entry in a commit before tagging.
- The built P4Stubs plugin in `private/stubs/Plugins/P4Stubs`. Make it with
  `py -3.11 tools/stubs/regenerate.py --build`, which reads the pinned inputs in
  `private/stub-inputs` ([`tools/stubs/inputs.json`](../tools/stubs/inputs.json)). Build it again when
  the generator changes.

## Build

On the laptop, make a clean checkout (or worktree) of the commit to release. Set `VersionName` in
`Kadan/Kadan.uplugin`, commit, and tag that commit `v<VersionName>`, such as `v0.4.0`. Then run:

```sh
py -3.11 tools/release.py build
```

Add `--prerelease` for a pre-release, and `--mirror N` to use another desktop mirror.

The command refuses, before anything is sent, when:

- the checkout has uncommitted or untracked files;
- HEAD does not carry the tag `v<VersionName>`, or carries a version tag that disagrees with
  `VersionName`.

It writes `private/release-source.json`: the commit, tag and version, and the SHA-256 of every tracked
file in `Kadan/`, `core/`, `compiler/`, `own_shaders/`, `navigation/` and `tools/`. It sends every
tracked file and that record to the mirror and runs `tools/build_native_release.py` there in the
background. The desktop build refuses when:

- a file the record lists is missing from the mirror or has other bytes, or one of those folders
  holds a file in the mirror that the record does not list, so the build reads the tag's files and
  nothing else. Only Python's `__pycache__` folders are let be. The message names the files; a file
  the tag does not have is deleted from the mirror by hand;
- `retoc.exe` or `repak.exe` differs from its pin;
- the P4Stubs plugin is not built, is not enabled by default, has other modules, or was built for
  another Unreal build than Kadan;
- the finished archive holds game containers or assets, sources (P4Stubs' generated headers
  included), PDBs, Python, private folders or the compiled automation tests.

The command then fetches the archive and its `SHA256SUMS` into
`private/desktop-results/private/release-<version>/`, checks one against the other and checks that
the archive names the tag's commit. It prints the `gh release create --draft` command that uploads
them. If a step fails, `py -3.11 tools/remote_unreal.py log release-<version>` shows the build's output.

## What the archive holds

`Kadan-UE4-<version>.zip` (`Kadan-UE4-<version>-prerelease.zip` for a pre-release):

- `Kadan/`: the plugin's `Kadan.uplugin`, `Config/` and `Binaries/Win64` (the three modules and
  `UE4Editor.modules`), and `BUILD.json` with the version, tag and commit it was built from.
- `Kadan/Tools/`: `retoc.exe` and `repak.exe`, with their licenses in `Kadan/Tools/licenses/`.
- `P4Stubs/`: `P4Stubs.uplugin` and `Binaries/Win64` (six DLLs and `UE4Editor.modules`), without
  PDBs or `Source/`. P4Stubs holds compiled declarations of the game's own classes, structs and enums,
  so that the editor can open the game's assets. It is generated from the game's type information and
  holds none of the game's assets or code.
- `README.txt`: the version, commit and tag, what each folder is, the bundled tools and their
  licenses, and the install guide's address.

## Upload and publish

Run the printed command, which uploads the archive and `SHA256SUMS` as a draft release of the tag.
Open the draft on GitHub, write the release notes and publish it. Publishing starts the
[Discord announcement](DISCORD.md), which posts the notes and a link to the release.
