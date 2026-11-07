# Contribute to Kadan

`core/` is the source for the portable code staged into the plugin.
Add a portable source file to `tools/native_core_files.txt` when the plugin needs it.

## Setup

Install Python 3.11.9, LLVM 22.1.1, and Node.js 24.14.0. Put LLVM's `clang++` on `PATH`.
The version files in the repository record the Python and Node.js versions used by CI.

Create and activate a Python environment:

```sh
python -m venv .venv
```

On Windows PowerShell, run `.venv\Scripts\Activate.ps1`. On Linux or macOS, run
`source .venv/bin/activate`. Then install the development dependencies and docs packages:

```sh
python -m pip install -r requirements-dev.txt
npm --prefix site ci
```

`requirements-dev.txt` pins the Lua runtime used by the preview-controller tests.
`site/package-lock.json` pins the docs dependencies.

## Checks

Run the repository gate before a commit:

```sh
python tools/check.py
```

To reproduce the stricter hosted gate and write its report:

```sh
python tools/check.py --ci --report build/ci/checks.json
```

The gate compiles the portable C++ tests and runs the tool tests, including the stubs
generator checks. The tests use authored fixtures. A missing required dependency or an
unexpected skipped test fails CI. Use `--core` or `--tools` for a narrower run while you work.

Build the docs and check their links with:

```sh
npm --prefix site run build
```

The native plugin requires licensed Unreal Engine 4.26.2. Keep editor and game test inputs
outside Git. [CI](docs/CI.md) separates the hosted checks from editor automation, and
[Releases](docs/RELEASING.md) describes the native qualification and publication gate.

## Pull requests

Keep each pull request focused on one change. Explain the resulting behavior and list the
checks you ran. State whether a result came from a portable host test, editor automation, or
the game. Preserve unrelated changes in shared checkouts and use a separate branch or worktree
for independent work.

Include authored shader sources and their provenance when changing a shipped shader payload.
Record the compiler and validation used. Container reconstruction and a GPU compilation result
are different checks.

## Content and license

Contribute original source, tests, and documentation under the [MIT license](LICENSE). Preserve
the licenses and credits of dependencies, development tools, and engine code. The project's
license covers its original code and does not replace their terms.

Keep game binaries, assets, saves, keys, private evidence, workstation configuration, and
personal tester references out of commits and release archives. Use authored fixtures for
public checks. Review the entire proposed Git history before publication; a deletion in a
later commit leaves the earlier content in history.
