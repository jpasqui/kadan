# Remote Unreal

`tools/remote_unreal.py` sends a checkout to a Windows desktop and runs the installed Unreal tools
there. It does not install Unreal or copy game data. Check the desktop's memory and running editors
before starting a job, and use one mirror per worker.

## Connection settings

Keep machine-specific values in `.remote/config.json`, which Git ignores. The file accepts these keys:

| Key | Environment variable | CLI option | Value |
| --- | --- | --- | --- |
| `host` | `P4_UNREAL_HOST` | `--host` | SSH host or SSH configuration alias |
| `root` | `P4_UNREAL_ROOT` | `--root` | Absolute Windows folder for the repository mirror |
| `key` | `P4_UNREAL_KEY` | `--key` | Optional SSH identity file on the calling machine |
| `engine` | `P4_UNREAL_ENGINE` | `--engine` | Optional absolute Unreal installation folder on the desktop |
| `python` | `P4_UNREAL_PYTHON` | `--python` | JSON array of the remote Python command and its arguments |
| `ssh` | `P4_UNREAL_SSH` | `--ssh` | Optional local SSH client executable |
| `paks` | `P4_UNREAL_PAKS` | `--paks` | Paks folder relative to the mirror, default `private/game-paks` |

CLI options override environment variables, which override the JSON file. Use `--config` or
`P4_UNREAL_CONFIG` to select another local JSON file. Missing host or root settings stop the command
before it attempts a connection. The remote Python command defaults to `["py", "-3.12"]`.
If no engine folder is configured, the desktop's build tool can use its own installation settings.

For example, set `P4_UNREAL_HOST` and `P4_UNREAL_ROOT` in the calling shell, then run:

```powershell
python tools/remote_unreal.py sync
python tools/remote_unreal.py --mirror 2 run --background build -- py tools/dev_build.py --tests
python tools/remote_unreal.py --mirror 2 wait build
```

`--mirror 2` and `--mirror 3` add ` 2` and ` 3` to the configured base folder. `P4_UNREAL_MIRROR`
sets the default mirror number. Each mirror keeps its own build outputs and editor state.

The editor test input root is separate from the repository mirror. Pass it with
`tools/dev_tests.py --input-root` when the private inputs live elsewhere. Keep those inputs,
connection files, and their paths out of source commits and release archives.
