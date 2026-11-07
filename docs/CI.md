# CI

GitHub Actions checks the portable code, development tools, and documentation on source changes.
The native plugin has a separate release qualification gate.

## Environment

| Tool | Version | Source |
| --- | --- | --- |
| Python | 3.11.9 | [Python releases](https://www.python.org/downloads/release/python-3119/) |
| LLVM | 22.1.1 | [LLVM releases](https://github.com/llvm/llvm-project/releases/tag/llvmorg-22.1.1) |
| Node.js | 24.14.0 | [Node.js release](https://nodejs.org/download/release/v24.14.0/) |
| Lupa | 2.8 | `requirements-dev.txt` |
| Docs packages | Locked | `site/package-lock.json` |

Install Python packages with `python -m pip install -r requirements-dev.txt`, and docs packages
with `npm --prefix site ci`. Put LLVM's `clang++` on `PATH`.
[CONTRIBUTING.md](../CONTRIBUTING.md#setup) gives the local setup steps.

## Hosted checks

Pull requests and pushes to `main` run the portable gate on Windows and build the docs on
Ubuntu. Dependency updates arrive as monthly pull requests grouped by ecosystem. Branch pushes
use the pull request's checks to avoid duplicate builds.

Run the same strict gate locally with:

```sh
python tools/check.py --ci --report build/ci/checks.json
```

The gate compiles and runs the portable C++ programs and discovers the Python tool tests,
including the stubs generator tests. The Lua preview-controller checks use Lupa. Tests use
authored fixtures and require no game dump, private receipt, licensed editor, or desktop
connection. Required dependencies are checked before the suite runs, and unexpected skips fail
the gate. The report records what ran and the result.

Use `python tools/check.py --core` or `python tools/check.py --tools` for a focused local run.
The default `python tools/check.py` runs both groups.

The docs job installs the locked packages with `npm ci`, builds the site, and checks internal
links. The `kadan-checks` artifact holds the check report, and `kadan-docs` holds the built
docs for the tested revision.
Ordinary check jobs have read-only repository permissions.

## Native qualification

Portable checks exercise the core and tooling contracts. Building the editor plugin requires
a licensed Unreal Engine 4.26.2 installation, and native automation uses separately configured
editor inputs. Game acceptance runs against the resulting mod. Record these results separately.

The existing native release policy binds qualification to an exact archive and source. It
requires the editor and game acceptance chain specified by `tools/native_quality_policy.json`.
A hosted check result does not replace that qualification.

[Releases](RELEASING.md) describes the trusted manual release workflow. It uses a qualified
artifact supplied on a configured native release runner and creates a draft after verification.
Public pull request jobs have no connection to that environment.

## Releases and announcements

Review the draft, edit its patch notes, and publish it when the release is ready. The
[Discord announcement](DISCORD.md) uses the published release's description and link. A webhook
can be configured after the release channel or thread exists. Draft releases and CI builds
send no Discord announcement.

## Repository settings

After the first workflow run, require the displayed `Portable checks` and `Documentation` jobs
for `main`. Keep normal workflow tokens read-only and release writes scoped
to the trusted release job. Branch rules and runner registration are GitHub settings; source
files describe the checks but do not configure those account settings.
