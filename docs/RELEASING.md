# Native releases

The hosted CI workflow checks portable code, tools, and documentation. A native plugin release
also needs a licensed Unreal Engine 4.26.2 build and acceptance evidence for the exact archive.
The release workflow admits an archive that has completed those checks. It does not build the
plugin or replace editor and game acceptance with hosted tests.

## Qualify the candidate

Build with `tools/build_native_release.py` on the licensed build machine. The build writes
`acceptance_required: true`; building the archive does not qualify it for release.

Complete the checks in `tools/native_quality_policy.json` against that archive and the source
revision to be tagged. The acceptance chain imports one original map, compares it with the
game's files, edits it, saves and reopens it, exports it, checks the export for game assets,
and observes the edits in the game. Every link names the same map. Required checks must pass,
source hashes must match, the archive hash must match, and no regression may remain open.

Run `tools/native_quality_gate.py` with the artifact and private evidence when preparing
the qualification record. Keep that record and the original game inputs private.

## Configure the runner

Use a trusted, dedicated self-hosted Windows x64 runner with the custom label `kadan-release`.
Give its runner process an environment variable named `KADAN_RELEASE_ROOT`, pointing to a
private qualified-artifact directory outside the runner's source checkout. Do not use an
active developer checkout as the runner work folder. The runner needs PowerShell 7, Git,
Python support for `actions/setup-python`, and an installed GitHub CLI (`gh`). The workflow
supplies its repository token to the publish step. No Unreal installation is invoked.

Create a GitHub environment named `native-release` and apply the repository's available
approval and branch restrictions. Run the workflow from `main` only. Public pull requests
never use this runner. No runner is registered or engine/game job started by repository setup.

Under `KADAN_RELEASE_ROOT`, create a folder named for the existing release tag, such as
`v0.3.0-rc.1`. Put these three files in it:

- `Kadan-UE4.zip`: the exact qualified native archive.
- `qualification.json`: evidence accepted by the existing native quality gate.
- `release-notes.md`: UTF-8 patch notes for the release description.

The inputs and their parent directories must be ordinary local files and directories, with
no symbolic links or junctions. The tag's version must equal `VersionName` in `Kadan.uplugin`.
Tag the source revision whose files were qualified. A source snapshot or packaging change
requires matching qualification; earlier private-tree evidence is not accepted automatically.

## Prepare a draft

Run **Draft native release** from the `main` branch in GitHub Actions and enter the tag.
Hosted checks run for that tag first. The trusted job then checks out the exact commit they
validated, verifies that the tag still names it, and runs the native qualification gate.
The archive also passes the existing positive runtime inventory and module-map checks.

The workflow stages a copy of the unchanged qualified archive, a license, a public receipt,
and checksums under `build/github-release`. It uploads those files directly as draft release
assets. The raw evidence, game inputs, local paths, and licensed editor files are not uploaded.
An unsuccessful check stops release creation. A prerelease tag creates a prerelease draft.

The local preparation command is:

```sh
python tools/github_release.py --release-root QUALIFIED_ARTIFACT_ROOT --tag v0.3.0-rc.1
```

Review the draft and its patch notes, then publish it in GitHub. The
[Discord announcement workflow](DISCORD.md) posts the published description and a release
link after its webhook is configured. Publishing a draft is a separate maintainer action.
