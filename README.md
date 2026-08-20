# Mew Cat Part Framework
A DLL dependency mod that allows other mods to add their own custom non-conflicting cat parts and textures!

<img width="500" height="357" alt="preview" src="https://github.com/user-attachments/assets/1511dcbe-8579-4a46-b50e-7ecd4aa0dd45" />

> **Current support:** This framework currently only applies to **cat body parts and cat textures**. Other moddable content such as items is **not supported yet**, but support for additional content types is planned soon.

# Making a Custom Cat Part Mod

NOTE: A "cat_parts_test" FLA file demonstrating multiple cat part/texture additions is included in the root directory of this repo, feel free to download it and use it as an example!

To make a cat part mod, install **MewCatPartFramework**, then create your own mod folder next to it.

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
      swflist.gon.append
```

Load your SWF from `swfs/swflist.gon.append`:

```text
game [
    your_cat_parts.swf
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
```

* `id` is the name you will reference from GON, such as `@myMod.bodyA`
* `kind` is the type of cat part being added
* `appendBatch` identifies the group of SWF timeline appends this part belongs to
* `logicalPartIndex` is **1-based, not 0 based** (despite the name) and selects the part's position inside that appended batch

Custom ActionScript linkages are used to identify your textures/part on the FLA/SWF side of things.
Your SWF ActionScript linkage uses the same batch ID:

```text
_Append_<CatTarget>__MCPF__<appendBatch>
```

For example:

```text
_Append_CatBody__MCPF__myMod.catParts
```

The batch name after `__MCPF__` must match the batch name used in `cat_parts.txt`.

When adding a new cat body part, make sure the part includes the **most up-to-date vanilla `tex` child timeline** for that respective body part in the FLA/SWF. You can obtain the `tex` timeline by decompiling the game's current `catparts.swf` and copying it from the equivalent vanilla part.

Supported part targets are:

| Kind      | Required SWF target(s)                                         |
| --------- | -------------------------------------------------------------- |
| `body`    | `CatBody`                                                      |
| `head`    | `CatHead`                                                      |
| `leg`     | `CatLeg` (Used by both legs and arms)                          |
| `tail`    | `CatTail`                                                      |
| `ear`     | `CatEar`                                                       |
| `eye`     | `CatEye`, `CatEye_Right`, `CatEyeClosed`, `CatEyeClosed_Right` |
| `eyebrow` | `CatEyebrow`                                                   |
| `mouth`   | `CatMouth`, `CatMouthOpen`, `CatMouthSmile`                    |
| `texture` | **(all five texture targets listed in the "Cat Textures" section of this tutorial)**                      |

Kinds with multiple targets (like eyes or mouths) must append matching logical slots to every required target.

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

## Other Notes

Due to an accidental mismatch in the original cat assets, (at the time of writing at least), the **Head texture timeline is 1505 frames long**, while the other texture timelines are **1506 frames long**.
MewCatPartFramework detects and corrects this mismatch automatically when custom textures are appended. **Do not add an empty padding frame yourself.** Manual padding will interfere with the framework's alignment handling.
