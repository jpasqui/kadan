---
title: Troubleshooting
description: How to find out why an import or an export isn't working.
---

If something isn't working, the first place to look is Unreal's Output Log. Kadan shows you
the affected assets in its own panel, and it writes the details to the log.

## Problems

| If this happens | Try this |
| --- | --- |
| There is no Kadan menu | Make sure that the `Kadan` and `P4Stubs` folders are both inside of your project's `Plugins` folder, and that you opened the project in Unreal Engine 4.26. |
| The game folder doesn't load | Choose your dump's `romfs` or `Paks` folder. The `Paks` folder must contain `Carrot4-Switch.pak`, `Carrot4-Switch.utoc`, and `Carrot4-Switch.ucas` together. If the Import page asks for retoc or repak, choose them, then choose the game folder again. |
| Kadan asks you to restart Unreal Editor | Save your work and restart the editor so it loads the project settings that Kadan prepared. |
| The list shows no matching assets | Clear the search box, choose **All content** in the kind list, and click the top of the folder tree. |
| The Output Log says that a package is a duplicate | Your game folder has more than one copy of the game in it. Select a folder with only one. |
| An import reports a missing file | The Output Log names the affected package. Check the selected dump, then choose the game folder again before retrying. |
| The Import page says that Kadan can't find its P4Stubs plugin | Close Unreal Editor and put the `P4Stubs` folder from the Kadan zip beside the `Kadan` folder in `Plugins`. Replace both folders when you update. |
| The Import page says that Unreal did not load a P4Stubs module | Use the `P4Stubs` folder from the same Kadan zip as the `Kadan` folder, and open the project in Unreal Engine 4.26. The Output Log says why Unreal didn't load it. |
| An import was interrupted | Start the same import again. Kadan keeps what already finished and continues from there. |
| An imported asset won't let you edit or save | It's read-only. Hover over it in the Content Browser or read its Details for the reason, then use the supported editing route. |
| What ships asks you to check the game folder | Check the dump selected on the Import page, and choose it again. |
| What ships asks you to open the map | Open the persistent level of the game map that you changed, the one whose name ends in `_P`. |
| What ships lists something that can't ship | Read the reason on it, fix it or click **Remove** for the item that brought it in. The list updates by itself. |
| An export stops | Kadan shows why in a window and in the Output Log. If it says that the game has the mod's files open, close the game and export again. |
| **Export mod** is grayed out | Choose a mod, add content, and pick a folder in **Export to**. |
| What ships says that an asset holds the game's content | Kadan refuses a texture, static mesh, or sound of yours that holds exactly the content of one of the game's assets, such as an unchanged copy. Change the asset, or use a file of your own. Check [Asset ownership](/guides/export/#asset-ownership). |
| The export says that an actor is exported without baked lighting | Read the reason in the export result. Kadan bakes static mesh actors that can't move and aren't attached to another actor. If their map wasn't open in the editor, open it and export again. Check [Lighting and navigation](/guides/export/#lighting-and-navigation). |
| The export says that lighting can be rebuilt for one edited map per export | Export the other map's lit changes in another mod. |
| A game material's blend or fade doesn't show on your mesh, and the edge is hard | The material reads your mesh's painted vertex colors, and Unreal's FBX import ignores them unless you ask for them. In the import dialog, or in the mesh's **Asset Details** before you reimport it, set **Vertex Color Import Option** to **Replace**. The export notes name a mesh that came in without its colors. |
| A soft moss or ground edge is hard in the editor | These materials fade by scene depth, which the editor can't draw on an opaque material. Kadan marks them **Preview differs from the game**. Check the edge in the game. |
| A mesh that you added draws dark in the editor | The game's lights are baked, so a new mesh has no lighting until a build. Use **Build** > **Build Lighting Only**, then save the map. |
| Saving a game map asks to make its `_BuiltData` file writable, or says it can't save it | Update Kadan. Kadan 0.3.1 tried to save the game's built lighting with the map. Kadan now saves a lighting build that you made in the editor, and leaves the game's built lighting as it is otherwise. |
| Another mod is named in a claim conflict | Check [Two mods on at once](/guides/several-mods/#two-mods-on-at-once). Shared map edits and stackable values can coexist, while exclusive claims need a choice. |

## Reports

If none of these help, then [report the problem](/guides/get-help/) on the Discord, and include
your log file, as this will help us find out what went wrong.
