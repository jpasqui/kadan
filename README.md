# Kadan

Kadan is the Unreal Engine 4.26 editor plugin for making Pikmin 4 mods. It imports maps and content
from a player's own game dump, lets you edit them and add your own, and exports a mod that
[Tsuchi](https://tsuchi.jpasqui.dev) loads.

The guides are on [kadan.jpasqui.dev](https://kadan.jpasqui.dev). Releases are on
[GitHub](https://github.com/jpasqui/kadan/releases), and help is on [Discord](https://discord.gg/Rg5BnvuwmQ).

- [CONTRIBUTING.md](CONTRIBUTING.md): development setup and pull requests.
- [CI](docs/CI.md): hosted checks and documentation builds.
- [Releases](docs/RELEASING.md): qualified plugin archives and draft releases.
- [Discord announcements](docs/DISCORD.md): published release notes and webhook setup.

Kadan's original source uses the [MIT license](LICENSE). Dependencies and development tools
keep their own licenses. Building and qualifying the native plugin requires a licensed Unreal
Engine installation and the inputs described in the release guide.

## Layout

| Path | Holds |
| --- | --- |
| `Kadan/` | The plugin: `Kadan.uplugin`, `Config/` and the modules in `Source/`. `Kadan` is the editor module, `KadanShaderFormat` the Switch shader format, `P4BlueprintImport` the Blueprint importer, and `P4MapKitTests` the automation tests, which only development builds include. |
| `core/` | The portable C++ the plugin compiles, with no Unreal in it. The build copies the files `tools/native_core_files.txt` names into the plugin. |
| `compiler/` | The Maxwell shader compiler the plugin builds for its own materials. |
| `own_shaders/` | The vertex and geometry programs Kadan ships for its own materials, as payloads. |
| `navigation/` | The format of a mod's own navigation data. |
| `tests/` | The host tests of `core/`, plain C++ programs. |
| `tools/` | The development build, the automation test runner, the release build, map sweeps and the desktop runner, with their tests. `tools/stubs/` generates the P4Stubs plugin, which declares the game's types. |
| `site/` | The docs site. |
| `docs/` | Technical notes, CI and releases. |

`private/` holds inputs and outputs that are not committed: the game's files, built hosts and test projects.

## Checks

The development environment uses Python 3.11.9, LLVM 22.1.1, and the Python packages in
`requirements-dev.txt`. [CONTRIBUTING.md](CONTRIBUTING.md#setup) gives the setup steps.

```
python tools/check.py
```

This compiles and runs the host tests in `tests/` and runs the tool tests in `tools/`, including
the stubs generator checks. The hosted gate uses authored fixtures and requires no game dump
or editor installation. To run the stricter CI gate and write its report:

```
python tools/check.py --ci --report build/ci/checks.json
```

## Building the plugin

The plugin is built and tested on the desktop with Unreal's build tool:

```
py -3.11 tools/remote_unreal.py run --background build -- py tools/dev_build.py --tests
py -3.11 tools/remote_unreal.py wait build
py -3.11 tools/remote_unreal.py run --background tests --session -- py tools/dev_tests.py --built private/mapkit-native-workflow/dev-host
py -3.11 tools/remote_unreal.py wait tests
```

`dev_build.py` copies the plugin into the host project (`private/mapkit-native-workflow/dev-host` by
default), stages `core/` and `compiler/` into it, and builds only what changed. `--tests` adds the
`P4MapKitTests` module. `dev_tests.py` installs the built plugin into a project that has P4Stubs
(`private/mapkit-native-workflow/native-coverage-clean-host` by default) and runs the fast automation
tests there; the editor needs a desktop session, hence `--session`. The release archive is built with
`tools/build_native_release.py`.

Native editor and game checks use separately configured local inputs: the game's Paks,
retoc and repak, the test project, and the game files the mesh tests read. Those inputs stay
outside Git. A host check, an editor automation result, and an observation in the game are
recorded separately. The native release gate checks the exact archive and source identity.

## Docs site

The site is built with Starlight and uses Node.js 24.14.0.

```
cd site
npm ci
npm run build
```

This writes `site/dist/`.
