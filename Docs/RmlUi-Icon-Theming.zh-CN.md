# rbfx 游戏里的 RmlUi 图标与主题覆盖

`[English](RmlUi-Icon-Theming.md)`

讲两件事：主题自带的图标（勾选框的勾、单选圆点、箭头、关闭按钮、缩放手柄）在缩放的画布上为什么糊；以及怎么把自己的图标接进项目样式表。

适用范围：RmlUi 画布**做过缩放**的项目（`RmlUi` 组件上的 `SetScale`）。画布 1:1 的项目不受影响。

---

## 1. 现象

| 查看位置 | 结果 |
|---|---|
| 编辑器里双击 `.rml` 预览，或任何 1:1 的文档 | 勾/圆点/箭头都正常 |
| 游戏里，缩放过的画布 | 同一个图标糊成一团白，或者干脆画出了**别的**图标的碎片 |

## 2. 根因

1. `Data/UI/default.rcss` 的图标来自 `@spritesheet theme { src: /Textures/RmlUI-1x.png; }`，也就是 **16 x 16** 那一档密度（勾只有 16 px 的美术）。
2. 缩放画布上一个 16 dp 的勾选框只有 ~9 屏幕像素。16 px 的美术先被缩到一半，渲染目标还可能再放大到物理面板，勾的 2 px 笔画就化成一团。
3. 引擎 `bin/Data/Textures` 里带齐了四档密度：`RmlUI-1x.png`、`RmlUI-1.25x.png`、`RmlUI-1.5x.png`、`RmlUI-2x.png`，布局完全一致，只是倍率不同。主题用的是最小那档。
4. **媒体查询救不了**：`@media (min-resolution: 2x)` 比较的是**上下文的 dp ratio**，不是 `SetScale` 传进去的系数（见 `Source/Core/StyleSheetContainer.cpp` 的 `MediaQueryId::Resolution` / `MinResolution`）。用 transform 缩小的画布 dp ratio 仍是 1，这类查询永远不会命中。

所以解法是在项目样式表里改用 2x 图集重登记同一个图标，见 §4。

## 3. 三条会咬人的规则

**（a）sprite 矩形是贴图原始像素。**
`@spritesheet 名字 { 图标: <x> <y> <w> <h> }` 是**图集图片上**的矩形，在 `DecoratorTiled::Tile::CalculateDimensions` 里直接换算成 UV，既不是显示单位，也不乘/除 `resolution`。

`resolution` 只参与 `display_scale = 1 / resolution`，用于图块的**自然尺寸**（`原始尺寸 x dp ratio x display_scale`）——也就是只在元素的宽高是 `auto` 时才起作用。而 `decorator: image(...)` 画进定尺寸元素时是拉伸填满，`resolution` 写多少都不影响你看到的画面。

**后果**：把图标从 1x 图集换到 2x 图集，矩形要**翻倍**。照抄 1x 的矩形会采到隔壁的图标，而且**不报错、没有警告**，症状恰好就是"糊白块"或"画出了别的字符"。坐标见 §5。

**（b）用同名 sprite 重新登记，不会追溯升级主题的规则。**
decorator 是在样式表解析时就实例化的：`default.rcss` 加载的那一刻，`input.checkbox:checked { decorator: image(check-mark); }` 就已经绑到了 1x 的 sprite 上。之后合并进来的精灵表（哪怕同名）不会让已解析的属性重新实例化。所以必须**另起 sprite 名字**，并在 `default.rcss` 之后用自己的样式表把同选择器的规则重写一遍。

**（c）`decorator: image(...)` 会拉伸填满元素的 padding box**（默认 `fit: fill`），不保持比例。图标美术做正方形，比例交给元素尺寸控制。

## 4. 模板

```css
/* 项目自己的样式表，link 在 default.rcss 之后。 */
@spritesheet theme2x {
    src: /Textures/RmlUI-2x.png;   /* png 和 .xml 都要拷进项目 Data/Textures */
    resolution: 2;                 /* 自然尺寸 = 原始像素 / 2 */
    check-mark-2x: 72px 32px 32px 32px;
    radio-dot-2x: 40px 32px 32px 32px;
}

input.checkbox:checked { decorator: image(check-mark-2x); }
input.radio:checked    { decorator: image(radio-dot-2x); }
```

记住屏幕上的**像素**尺寸 = dp 值 x UI 缩放系数。0.55 的画布上，26 dp 的勾选框约 14 px：32 px 的勾缩到 14 px 还认得出，16 px 的勾就认不出了。

## 5. 图集坐标（两张图都实测过）

布局一致，2x 的矩形正好是 1x 的两倍。

| sprite | `RmlUI-1x.png`（64x64） | `RmlUI-2x.png`（128x128） |
|---|---|---|
| `close-button` | `0,0,20,20` | `0,0,40,40` |
| `arrow-right` | `20,0,16,16` | `40,0,32,32` |
| `arrow-down` | `36,0,16,16` | `72,0,32,32` |
| `radio-dot` | `20,16,16,16` | `40,32,32,32` |
| `check-mark` | `36,16,16,16` | `72,32,32,32` |
| `resize-handle` | `36,32,16,16` | `72,64,32,32` |

## 6. 加自己的图标

1. png 放进**项目**的 `Data/Textures`（引擎 `bin/Data` 里的东西不会打进你的游戏）。美术做正方形，按你会画到的最大尺寸的 2 倍左右出图。
2. 登记：`@spritesheet my_icons { src: /Textures/my.png; iconName: 0px 0px 64px 64px; }`，矩形用你自己 png 的原始像素。
3. 挂上：`#某个元素 { decorator: image(iconName); }`。
4. sprite 名在文档作用域内是全局的，同名会静默互相覆盖，项目里的名字加前缀。

## 7. 别猜，直接验证矩形

png 才是事实。把候选矩形打成 ASCII 透明度图：

```powershell
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile("$PWD\bin\Data\Textures\RmlUI-2x.png")
$chars = ' .:-=+*#%@'
for ($y = 32; $y -lt 64; $y++) {        # 候选矩形的 top..bottom
    $line = ""
    for ($x = 72; $x -lt 104; $x++) {   # left..right
        $line += $chars[[int]($bmp.GetPixel($x, $y).A / 255.0 * 9)]
    }
    $line
}
$bmp.Dispose()
```

图是空的或者偏出边界，就是矩形写错了。

## 8. 自查清单

- [ ] 画布缩放系数明显小于 1 时，图标取自 2x（或更大）的图集。
- [ ] 矩形写的是贴图原始像素，不是显示单位。
- [ ] 覆盖用的 sprite 换了新名字，同选择器的规则在 `default.rcss` 之后重写过。
- [ ] 图集 png/xml 在项目的 `Data/Textures` 里，不只是 `bin/Data`。
- [ ] 在真机（或按真实缩放跑的游戏）上验证过——编辑器预览是按 1:1 跑的，游戏里出问题它照样显示正常。