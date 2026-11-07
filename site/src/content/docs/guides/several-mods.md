---
title: Work on several mods
description: How to keep more than one mod in the same Unreal project.
---

You can keep as many mods as you want in one project. Each mod has its own ID, name, and version,
and its own list of what it includes, so you can work on one mod without changing the others.

## Mods

To add a mod, open **Kadan > Mods and export** and click **New mod**. Enter the **Name**, check
its suggested **ID**, enter the **Version**, and click **Save**. The mod should now appear in the
list at the top of Mods.

To switch to another mod, choose it in the list. Kadan shows what it includes, and What ships
updates by itself.

To change the open mod's name or version, click **Edit**. The ID stays the same once you've saved
the mod, since it names the mod's folder and files and Tsuchi uses it to tell mods apart. If you
need a different ID, make a new mod.

## IDs

A mod's ID can have up to 47 lowercase letters, numbers, dots, hyphens, and underscores, and it
has to start and end with a letter or a number. For example, `yourname.mod-name` works.
IDs that start with `tsuchi.` are reserved for Tsuchi, so you can't use them. The version is
three numbers with dots between them, like `1.0.0`.

## Shared assets

Your assets can be anywhere in your project, and several of your mods can ship the same ones.
Each mod ships its own copy at the asset's path. [Replacements](/guides/export/#replacements) of
the game's assets stay at the game asset's path, in each mod that has them.

## Selections

Each mod keeps its own selection. When you add or remove an asset or a folder, only the open mod
changes. Kadan saves them in your project's `Config` folder as you make them, and they're still
there after you reopen the project.

## Two mods on at once

:::caution[WIP]
Not every combination of mods has been checked in the game. Test the mods you want to use
together before sharing them.
:::

Mods that ship the same asset or change the same map can still be on together in Tsuchi:

| Change | Rule |
| --- | --- |
| Each mod adds its own sublevels or content | Both apply. |
| Both ship the same asset | The mod that starts later is the one the game uses. Tsuchi's log says so. |
| Both remove the same actor | The actor is removed once. |
| One removes an actor that another moves or changes | The removal wins. |
| Both change the same property, table field, or text | Mod order decides which value is used. |
| Both provide navigation for one map | One navigation set is used, and the overlap is reported. |
| Both need the same new-map slot | Only one can run until the player turns the other off. |

Dependencies run before the mods that need them. Tsuchi shows the overlaps in its mod list so the
player can choose an order.
