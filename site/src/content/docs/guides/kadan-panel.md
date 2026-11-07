---
title: The Kadan panel
description: The Kadan menu and the controls for importing and exporting mods.
---

Open the **Kadan** menu after [installing the plugin](/guides/install/). The panel is
an Unreal tab that you can dock beside the Details panel or keep in its own window.

## Menu

| Item | Opens |
| --- | --- |
| **Import from game** | The panel's Import page |
| **Mods and export** | The panel's Mods page |
| **Game connection** | The development connection to a running game |
| **Documentation** | These guides |

**Import** and **Mods** at the top of the panel switch pages. The line under them says
what just happened. When there's more to read, **Output Log** opens Unreal's Output Log,
where Kadan writes the full reasons and file paths.

## Import

Follow [Import from the game](/guides/import/) for the steps.

| Control | Use |
| --- | --- |
| **Game** | Choose the dump's romfs or Paks folder with **Browse**, or type it and press Enter. |
| **Set up project** | Shown when the project needs the game's settings. It replaces the project's collision and navigation settings and restarts Unreal. The first start after setup may take a while. |
| **Choose retoc**, **Choose repak** | Shown only when one of the tools is missing from Kadan's Tools folder. |
| Search, kind and folder tree | Narrow the list. The list starts on Maps. **In this project** shows what's already imported. Ticked items stay ticked when you change the filters. |
| **N shown** | Ticks or unticks everything the list shows. |
| **Import** | Imports the ticked items and what they need. When some are already in the project, it says **Reimport**, and Kadan lists what it will replace before it does. |

The line beside **Import** counts what the ticked items need, what's already in the
project, how much Kadan reads from the game, and the free space on the project's drive.

## Mods

Follow [Export a mod](/guides/export/) and [Several mods](/guides/several-mods/) for the steps.

| Control | Use |
| --- | --- |
| Mod list | Switches between the project's mods. |
| **Edit** | Changes the open mod's name and version. |
| **New mod** | Starts a mod: its name, ID and version. The ID names the mod's folder and files, and can't change after you save. |
| **Add selected** | Adds the maps, assets or folders selected in the Content Browser. What they need comes along. |
| **Remove** | Takes that item out of the mod. |
| **What ships** | What the mod exports, worked out by itself when the mod's content changes. Anything that can't ship comes first, with why and **Locate**. What the game already has is only counted. |
| **Export to** | Eden's mod folder when Eden is installed, or a folder you choose. |
| **Cook for the game without Tsuchi** | See [Without Tsuchi](/guides/export/#without-tsuchi). |
| **Export mod** | Checks the mod again and exports it. |

During export, a window shows the stage, the asset or map Kadan is on, and the stages
done. **Cancel export** stops before the next stage. After an export, the line under the
button says when and where it went, with **Open folder**.

## In the level

Right-click placed scenery and choose **Ground type** to set the ground the game uses for it:
footsteps, effects and what grows or sinks there. Save the meshes and the level after.

A game map's day, night and Olimar versions are its sublevels. Show and hide them in
**Window > Levels**.

## Materials

Material Details displays **Game settings**. Known fields have readable labels,
unknown values belong in **Advanced**, and serialized names remain searchable.
Scene clipping is uncompiled and read-only. Material instances show the game's
supported instance fields with override checkboxes. Unchecked fields inherit through
the parent chain. Enable an override to change its value, or click **Open parent**
to inspect the parent. Older imports without typed C4 settings need a reimport.
