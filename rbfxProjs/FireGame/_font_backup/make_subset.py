import os, sys, glob
sys.stdout.reconfigure(encoding="utf-8")
from fontTools import subset

# Regenerates Data/Fonts/NotoSansSC-Regular.otf from the FULL backup in this
# directory. Run this after adding ANY new on-screen text (RML documents, Lua
# narration/share strings): the shipped font only carries the glyphs listed
# here, a character outside the subset renders blank on device.
#   d:\openSrc\emsdk\python\3.13.3_64bit\python.exe _font_backup\make_subset.py
here = os.path.dirname(os.path.abspath(__file__))
proj = os.path.dirname(here)
backup = os.path.join(here, "NotoSansSC-Regular.FULL.otf")
target = os.path.join(proj, "Data", "Fonts", "NotoSansSC-Regular.otf")

# 1) Collect every character the game can render: all UI documents plus all Lua
# scripts (comments included on purpose - a few spare glyphs cost kilobytes,
# one missing glyph costs a blank on device).
chars = set(chr(c) for c in range(0x20, 0x7F))  # printable ASCII superset
for c in "，。！？：；、（）《》“”‘’…—·％℃×":
    chars.add(c)
src = []
for pattern in ("Scripts/**/*.lua", "UI/**/*.rml", "UI/**/*.rcss", "UI/**/*.xml"):
    src += glob.glob(os.path.join(proj, "Data", pattern), recursive=True)
for path in src:
    with open(path, "r", encoding="utf-8", errors="ignore") as fh:
        chars.update(fh.read())
chars = {c for c in chars if (not c.isspace() or c == " ") and c.isprintable()}
text = "".join(sorted(chars))
print("scanned files:", len(src), "| unique chars:", len(chars))

# 2) Subset the FULL font down to that set, keeping the name tables so the
# family name survives (ReloadFonts / fallback face lookups match on it).
opts = subset.Options()
opts.name_IDs = ["*"]
opts.name_languages = ["*"]
opts.name_legacy = True
opts.glyph_names = False
opts.recalc_bounds = True
opts.drop_tables = []
font = subset.load_font(backup, opts)
subsetter = subset.Subsetter(options=opts)
subsetter.populate(text=text)
subsetter.subset(font)
subset.save_font(font, target, opts)

# 3) Verify: every collected character must now be covered.
from fontTools.ttLib import TTFont
cmap = TTFont(target).getBestCmap()
missing = sorted({c for c in text if ord(c) not in cmap})
print("new font:", os.path.getsize(target), "bytes,", len(cmap), "glyphs")
print("missing after re-subset:", ("".join(missing) if missing else "none"))
