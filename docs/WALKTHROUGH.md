# Walkthrough

`tools/kadan_walkthrough.py` runs a new creator's first hour on the desktop and writes one page of it:
every step in order, with its result and a picture of the editor after it. It is editor automation.
Nothing in it runs the exported mod in the game.

```
py -3.11 tools/remote_unreal.py run --background build -- py tools/dev_build.py --tests
py -3.11 tools/remote_unreal.py wait build
py -3.11 tools/kadan_walkthrough.py --game private/game-paks --retoc private/retoc/retoc.exe --repak private/repak/repak.exe --earlier-project private/kadan-walkthrough-earlier
```

The page is `private/desktop-results/private/kadan-walkthrough/<name>/results/walkthrough.html`, with the
pictures inside it. The pictures, each part's `steps.json` and the editor logs are in the same folder.

## Parts

| Part | Project | Steps |
| --- | --- | --- |
| `first-hour` | A new project with only the Kadan and P4Stubs plugins, made for the run | Open the panel from the Kadan menu, type the game folder, choose retoc and repak, set the project up, restart, search for a map, tick it, import it, open it, Save Current Level, add a static mesh actor, save again, make a mod, add the map, export, and read the export's containers |
| `earlier-project` | A copy of a project that an earlier release imported the map into, with this build's plugins in place of that release's | The same without the search and the import |

The tests are `P4MapKit.Kadan.WalkthroughFirstHour` and `P4MapKit.Kadan.WalkthroughEarlierProject`, in
`P4KadanWalkthroughAutomation.cpp`. They are in the heavy tier. Without their inputs, or in another project
than the one the runner made for them, they report `P4MapKit test not run.`

## Inputs

Each input is a folder or file on the desktop, absolute or relative to the mirror's root. It comes from its
option or from the environment variable the tests read.

| Option | Variable | Value |
| --- | --- | --- |
| `--game` | `P4_KADAN_WALK_GAME` | The dump's `romfs` or `Paks` folder |
| `--retoc`, `--repak` | `P4_KADAN_WALK_RETOC`, `P4_KADAN_WALK_REPAK` | The tools. A development build has no `Tools` folder, so the Import page asks for them |
| `--earlier-project` | `P4_KADAN_WALK_EARLIER_PROJECT` | The folder of a project that the earlier release imported the map into. It is copied, never changed |
| `--map` | `P4_KADAN_WALK_MAP` | The map's package. The default is `Cave013_F00_P`, a one-floor cave whose levels all have built lighting |

`--parts` chooses the parts, `--name` the run's folder and background run name, `--mirror` the desktop
mirror, and `--built` the build host whose plugin is tested.

The exit code is 0 when every chosen part ran and passed, 1 when a step failed or the editor stopped, and 2
when a part did not run because an input was missing.

## What a step is

A step uses the panel as a person does where the panel has a control: real mouse and key events on the
buttons, fields and boxes, and the system's own file dialog for **Choose retoc** and **Choose repak**. Three
things are done for the creator instead:

- The map is opened with the call a double click in the Content Browser makes, and Save Current Level is
  run through the level editor's own command.
- The static mesh actor is placed by code, with a mesh the map already has.
- The walkthrough sets **Export to** to `Saved/KadanWalkthrough/export` in its project before the panel
  opens, so it never exports into Eden's mod folder.

Project setup ends in Unreal's question whether to restart. The test answers Yes, records the step, and
takes back the editor's own start of the next session. The runner starts the editor again and the test goes
on at the next step.

`steps.json` is written after every step. A step that was running when the editor ended is shown as where
the run stopped. Errors that the Output Log had during a step are listed with it. They do not fail the step.

## Recording

With `ffmpeg` on the desktop's PATH, the desktop's screen is recorded for the whole run as
`walkthrough.mp4`, and the page shows it. Without it, the page says that the run was not recorded. The
runner installs nothing.

## Other builds and game versions

- **2.0.0 dump**: pass its `Paks` folder as `--game`.
- **Release ZIP**: the tests are in the development build only. A run against the archive needs the
  archive's DLLs with the separate test module, as `tools/dev_tests.py --release-archive` installs them.
  The runner does not do that yet.
- **Earlier release**: make a project with that release's ZIP, set it up and import the map by hand, close
  the editor, and pass its folder as `--earlier-project`. No import records the Kadan version that made
  it, so the page can only show the folder it was given.
