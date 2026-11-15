# RmlUi Icons & Theme Overrides in rbfx Games

`[中文](RmlUi-Icon-Theming.zh-CN.md)`

Covers the built-in theme icons (checkbox tick, radio dot, arrows, close button, resize handle) and how to bring your own art into a project stylesheet.

Applies to any rbfx project whose RmlUi canvas is **scaled** (the `SetScale` call on the `RmlUi` component). Projects that render the UI 1:1 are not affected.

---

## 1. Symptom

| Where the document is viewed | Result |
|---|---|
| Editor preview (double-click a `.rml`), or any 1:1 document | tick / dot / arrow look right |
| In game, on a scaled canvas | the same icon is a blurred white blob, or a fragment of a *different* icon |

## 2. Root cause

1. `Data/UI/default.rcss` declares its icons from `@spritesheet theme { src: /Textures/RmlUI-1x.png; }` — the **16 x 16** density variant (the tick is 16 px of art).
2. A scaled canvas draws a 16 dp checkbox at ~9 screen px. 16 px of art gets downsampled to roughly half, and the render target may be upscaled again to the physical panel — the tick's 2 px strokes dissolve into a blob.
3. RmlUi ships four density variants in `bin/Data/Textures`: `RmlUI-1x.png`, `RmlUI-1.25x.png`, `RmlUI-1.5x.png`, `RmlUI-2x.png`. They share the exact same layout, only the scale differs. The theme uses the smallest one.
4. A media query does **not** help: `@media (min-resolution: 2x)` compares against the **context dp ratio**, not the factor passed to `SetScale` (`Source/Core/StyleSheetContainer.cpp`, `MediaQueryId::Resolution` / `MinResolution`). A canvas scaled down by a transform still reports a dp ratio of 1, so such a query never matches.

The fix is to declare the same icon from the 2x atlas in the project stylesheet — see §4.

## 3. Three rules that bite

**(a) Sprite rectangles are raw texture pixels.**
`@spritesheet name { icon: <x> <y> <w> <h> }` is a rectangle **in the atlas image**, converted straight to UVs in `DecoratorTiled::Tile::CalculateDimensions`. It is *not* in display units and is neither multiplied nor divided by `resolution`.

`resolution` only feeds `display_scale = 1 / resolution`, which is used for the *natural* size of a tile (`raw size x dp-ratio x display_scale`) — i.e. only when the element's width/height are `auto`. An icon rendered by `decorator: image(...)` into a fixed-size element is stretched to the element regardless, so the `resolution` value has no effect on what you see there.

**Consequence:** to move an icon from the 1x to the 2x atlas, double its rectangle. Copying the 1x rectangle samples a neighbouring icon instead — note this fails **silently**, there is no warning; the symptom is exactly "a blob" or "the wrong glyph". Cache the table in §5.

**(b) Re-registering a sprite name does not retro-upgrade the theme's rules.**
Decorators are resolved while the stylesheet is parsed: when `default.rcss` is loaded, `input.checkbox:checked { decorator: image(check-mark); }` is already bound to the 1x sprite. Merging a later spritesheet list (even under the same name) does not re-instance parsed properties. So declare a **new** sprite name and restate the selector in your own stylesheet, after the `default.rcss` link.

**(c) `decorator: image(...)` fills the element's padding box** (default `fit: fill`), so aspect ratio is not preserved and the art is stretched to the box. Keep icon art square and give the element the proportion you want.

## 4. Template

```css
/* Project stylesheet, linked after default.rcss. */
@spritesheet theme2x {
    src: /Textures/RmlUI-2x.png;   /* copy the png (and .xml) into Data/Textures */
    resolution: 2;                 /* natural size = raw px / 2 */
    check-mark-2x: 72px 32px 32px 32px;
    radio-dot-2x: 40px 32px 32px 32px;
}

input.checkbox:checked { decorator: image(check-mark-2x); }
input.radio:checked    { decorator: image(radio-dot-2x); }
```

Remember that the *pixel* size on screen is `dp value x UI scale`. A 26 dp checkbox on a 0.55 canvas is ~14 px; a 32 px tick downsampled to 14 px still reads as a tick, a 16 px one does not.

## 5. Atlas coordinates (verified, both variants)

Layout is shared; the 2x rectangle is exactly twice the 1x one.

| sprite | `RmlUI-1x.png` (64x64) | `RmlUI-2x.png` (128x128) |
|---|---|---|
| `close-button` | `0,0,20,20` | `0,0,40,40` |
| `arrow-right` | `20,0,16,16` | `40,0,32,32` |
| `arrow-down` | `36,0,16,16` | `72,0,32,32` |
| `radio-dot` | `20,16,16,16` | `40,32,32,32` |
| `check-mark` | `36,16,16,16` | `72,32,32,32` |
| `resize-handle` | `36,32,16,16` | `72,64,32,32` |

## 6. Adding your own icon

1. Drop the png in the **project** `Data/Textures` (the engine's `bin/Data` is not packaged into your game). Prefer square art, authored at roughly 2x the largest size you will draw.
2. Declare it: `@spritesheet my_icons { src: /Textures/my.png; iconName: 0px 0px 64px 64px; }` — rectangle in raw pixels of your png.
3. Attach it: `#some_element { decorator: image(iconName); }`.
4. Sprite names are global per document scope; identical names overwrite each other silently. Prefix project sprites.

## 7. Verifying a rectangle instead of guessing

The png is the ground truth. Print an ASCII alpha map of the candidate rectangle:

```powershell
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile("$PWD\bin\Data\Textures\RmlUI-2x.png")
$chars = ' .:-=+*#%@'
for ($y = 32; $y -lt 64; $y++) {        # top..bottom of the candidate rect
    $line = ""
    for ($x = 72; $x -lt 104; $x++) {   # left..right
        $line += $chars[[int]($bmp.GetPixel($x, $y).A / 255.0 * 9)]
    }
    $line
}
$bmp.Dispose()
```

An empty or off-centre map means the rectangle is wrong.

## 8. Checklist

- [ ] The icon is declared from the 2x (or larger) atlas when the canvas scale is well below 1.
- [ ] The rectangle is in raw texture pixels, not display units.
- [ ] The override sprite has a **new** name and its selector rule is restated after `default.rcss`.
- [ ] The atlas png/xml exists in the project `Data/Textures`, not only in `bin/Data`.
- [ ] Verified on a real device — an editor preview runs at scale 1 and will look correct even when the game does not.