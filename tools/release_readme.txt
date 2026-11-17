Kadan $version
Built from commit $commit, tag $tag, of https://github.com/jpasqui/kadan

Kadan is the Unreal Engine 4.26 editor plugin for making Pikmin 4 mods.

What is in this download

Kadan/
    The Kadan editor plugin. Kadan/BUILD.json names the commit and tag it was built from.

Kadan/Tools/
    retoc.exe and repak.exe, which Kadan runs to read the game's packages and to pack mods.
    Their licenses are in Kadan/Tools/licenses.

P4Stubs/
    P4Stubs holds compiled declarations of the game's own classes, structs and enums, so that
    Unreal Editor can open the game's assets. It is generated from the game's type information and
    holds none of the game's assets or code. Kadan does not load without it.

Install

Put the Kadan and P4Stubs folders in your project's Plugins folder. When you update, replace both.
The install guide: https://kadan.jpasqui.dev/guides/install/

Licenses

Kadan's own code is under the MIT license:
https://github.com/jpasqui/kadan/blob/$tag/LICENSE
It does not cover the game, whose types P4Stubs declares, Unreal Engine, or the bundled tools:
$tools
