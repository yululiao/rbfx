import os, sys, glob, unicodedata

here = os.path.dirname(os.path.abspath(__file__))
proj = os.path.dirname(here)
data = os.path.join(proj, "Data")

# 1) collect characters actually used in displayed text (UI + all Lua, incl. comments as superset)
src = []
src += glob.glob(os.path.join(data, "Scripts", "**", "*.lua"), recursive=True)
src += glob.glob(os.path.join(data, "UI", "**", "*.rml"), recursive=True)
src += glob.glob(os.path.join(data, "UI", "**", "*.rcss"), recursive=True)
src += glob.glob(os.path.join(data, "UI", "**", "*.xml"), recursive=True)

chars = set()
for f in src:
    with open(f, "r", encoding="utf-8", errors="ignore") as fh:
        chars.update(fh.read())

# superset: all printable ASCII + space (digits/letters/punct for HUD)
for c in range(0x20, 0x7F):
    chars.add(chr(c))
# a few common CJK punctuation that may appear
for c in "，。！？：；、（）《》“”‘’…—·％℃×":
    chars.add(c)

# drop control/space-only handling
chars = {c for c in chars if unicodedata.category(c)[0] != "C" or c == " "}

charset = "".join(sorted(chars))
with open(os.path.join(here, "charset.txt"), "w", encoding="utf-8") as fh:
    fh.write(charset)

cjk = sum(1 for c in chars if ord(c) >= 0x3000)
print("scanned files:", len(src))
print("unique chars:", len(chars), "| CJK-or-punct:", cjk)
