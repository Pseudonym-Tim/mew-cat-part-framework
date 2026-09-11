# Mew Cat Part Framework
A DLL dependency mod that allows other mods to add their own custom non-conflicting cat parts and textures!

<img width="500" height="357" alt="preview" src="https://github.com/user-attachments/assets/1511dcbe-8579-4a46-b50e-7ecd4aa0dd45" />

> **Current support:** This framework supports **cat body parts, cat textures, and item equipment parts** (`weapon`, `trinket`, `headItem`, `neckItem`, `faceItem`).

# Making a Custom Cat Part Mod

NOTE: Example FLA files are included in the root directory of this repo:
* `cat_parts_test.fla`: Demonstrates multiple cat part and texture additions.
* `item_parts_test.fla`: Demonstrates weapon, trinket, and equipment item additions.

Feel free to download them and use them as examples!

To make a cat part mod, install [**MewCatPartFramework**](https://www.nexusmods.com/mewgenics/mods/489), then create your own mod folder next to it.

First, make sure your `description.json` lists the framework as a dependency so players know to install it:

```text
"requirements": [
    "MewCatPartFramework>=1.0.0"
]
```

Your mod should look something like this:

```text
mods/
  MewCatPartFramework/
    MewCatPartFramework.dll
  YourCatPartMod/
    description.json
    preview.png
    cat_parts.txt
    swfs/
      your_cat_parts.swf
      your_item_parts.swf
      swflist.gon.append
```

Load your SWF from `swfs/swflist.gon.append`:

```text
game [
    your_cat_parts.swf
    your_item_parts.swf
]
```

## Adding/Registering Parts

Custom parts are registered in `cat_parts.txt` using:

```text
id = kind appendBatch logicalPartIndex
```

For example:

```text
myMod.mouthA = mouth myMod.catParts 1
myMod.bodyA = body myMod.catParts 1
myMod.legA = leg myMod.catParts 1
myMod.textureA = texture myMod.catParts 1
myMod.headGear = headItem myMod.itemsTest 1
```

* `id` is the name you will reference from GON, such as `@myMod.bodyA` or `@myMod.headGear`
* `kind` is the type of cat part or item part being added
* `appendBatch` identifies the group of SWF timeline appends this part belongs to
* `logicalPartIndex` is **1-based, not 0 based** (despite the name) and selects the part's position inside that appended batch

Custom ActionScript linkages are used to identify your textures/part on the FLA/SWF side of things.
Your SWF ActionScript linkage uses the same batch ID:

```text
_Append_<Target>__MCPF__<appendBatch>
```

*(For item parts, `__MIF__` is also supported as an alternate linkage marker: `_Append_<ItemTarget>__MIF__<appendBatch>`)*

For example:

```text
_Append_CatBody__MCPF__myMod.catParts
_Append_HeadItemF__MCPF__myMod.itemsTest
```

The batch name after `__MCPF__` (or `__MIF__`) must match the batch name used in `cat_parts.txt`.

When adding a new cat body part, make sure the part includes the **most up-to-date vanilla `tex` child timeline** for that respective body part in the FLA/SWF. You can obtain the `tex` timeline by decompiling the game's current `catparts.swf` and copying it from the equivalent vanilla part.

Supported part targets are:

| Kind       | Required SWF target(s)                                         |
| ---------- | -------------------------------------------------------------- |
| `body`     | `CatBody`                                                      |
| `head`     | `CatHead` (optional: `CatHeadPlacements`)                      |
| `leg`      | `CatLeg` (Used by both legs and arms)                          |
| `tail`     | `CatTail`                                                      |
| `ear`      | `CatEar`                                                       |
| `eye`      | `CatEye`, `CatEye_Right`, `CatEyeClosed`, `CatEyeClosed_Right` |
| `eyebrow`  | `CatEyebrow`                                                   |
| `mouth`    | `CatMouth`, `CatMouthOpen`, `CatMouthSmile`                    |
| `texture`  | **(all five texture targets listed in the "Cat Textures" section of this tutorial)** |
| `weapon`   | `Weapon`, `WeaponIcon`, `WeaponIcon_Worn`, `WeaponIcon_Broken` |
| `trinket`  | `Trinket`, `TrinketIcon`, `TrinketIcon_Worn`, `TrinketIcon_Broken` |
| `headItem` | `HeadItemF`, `HeadItemB`, `HeadItemIcon`, `HeadItemIcon_Worn`, `HeadItemIcon_Broken` |
| `neckItem` | `NeckItemF`, `NeckItemB`, `NeckItemIcon`, `NeckItemIcon_Worn`, `NeckItemIcon_Broken` |
| `faceItem` | `FaceItemF`, `FaceItemB`, `FaceItemIcon`, `FaceItemIcon_Worn`, `FaceItemIcon_Broken` |

Kinds with multiple targets (like eyes, mouths, or item equipment) must append matching logical frame slots to every required target.

### Item Equipment & Icons

When adding custom items using `item_parts_test.fla`:
* Items require matching logical frames across their sprite layers and icon targets (e.g. `HeadItemF`, `HeadItemB`, and `HeadItemIcon`).
* For item icons, simply copy your icon symbols across the standard, `_Worn`, and `_Broken` versions in the FLA (e.g. `HeadItemIcon`, `HeadItemIcon_Worn`, and `HeadItemIcon_Broken`).

### Cat Head Placements

Custom heads can optionally provide matching `CatHeadPlacements` data for positioning of eye, ear, mouth, etc.

`CatHeadPlacements` is **not registered in `cat_parts.txt`**. It is companion data for a custom `head`, but it should use the same append linkaging:

```text
_Append_CatHead__MCPF__myMod.catParts
_Append_CatHeadPlacements__MCPF__myMod.catParts
```

The placement frames must use the same logical order as the corresponding `CatHead` frames.

## Cat Textures

Cat textures require a **complete set of five ActionScript linkages**, even if your texture is only intended to visibly affect one particular body part:

```text
_Append_CatBodyTexture__MCPF__myMod.catParts
_Append_CatHeadTexture__MCPF__myMod.catParts
_Append_CatLegTexture__MCPF__myMod.catParts
_Append_CatTailTexture__MCPF__myMod.catParts
_Append_CatEarTexture__MCPF__myMod.catParts
```

**Do not provide only one of these.** Every custom texture slot needs matching entries across all five cat texture timelines.

## Using Your Parts in GON

Once a part is registered, reference its ID with `@` anywhere the matching cat-part field is expected:

```text
MyCustomCat {
    mouth @myMod.mouthA
    texture @myMod.textureA
    body @myMod.bodyA
    arm1 @myMod.legA
    arm2 @myMod.legA
    leg1 @myMod.legA
    leg2 @myMod.legA
}
```

For custom items, reference the ID under the `frame` property:

```text
MyCustomHat {
    name "My Cool Hat"
    kind head
    frame @myMod.headGear
}
```

## Uninstaller
A tool for uninstalling custom cat parts from your save files has been included!

## Other Notes

MewCatPartFramework detects and corrects any timeline frame mismatches automatically when custom textures are appended. **Do not add an empty padding frame yourself!** Manual padding will interfere with the framework's alignment handling.
