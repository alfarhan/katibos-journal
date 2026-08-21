# Arabic face generation

The editor's Arabic faces are u8g2 arrays built from IBM Plex Sans Arabic Bold.
Coverage must include **FE70–FEFF** (the presentation forms `bidi::layoutLine`
emits) or every shaped letter renders blank, and the ASCII range must be present
too — bdfconv takes the header's ascent/descent from it, and an Arabic-only build
comes out with paragraph height 0.

```sh
brew install otf2bdf
git clone --depth 1 https://github.com/olikraus/u8g2.git
make -C u8g2/tools/font/bdfconv CFLAGS="-O2 -Wall"   # -O4 trips -Werror on clang
python3 gen_tuned.py                                 # writes faces3/*.c
```

Paragraph height (bytes 15/16 of the u8g2 header) sets the pitch, so a
replacement face must match the one it replaces: **19 for `_m`, 22 for `_l`**.
px21 gives 19 and px25 gives 22 for this typeface.

## Which face is which

| family | source | note |
|---|---|---|
| `ibmplex` | px21 **hinted** | what shipped before 1.15.0 |
| `plexclean` | px21 unhinted | hinting sheds stray single pixels on Arabic curves |
| `plexbig` | px23 hinted | largest whose paragraph height (20) still clears Compact |
| `plextuned` | px21 unhinted + `tune.py` | every 1px-wide stroke evened out to 2px |

`tune.py` exists because this panel needs two adjacent dots to read black. The
rasteriser leaves 1px fragments through yeh, kaf, ain, heh, noon and meem — and,
worst of all, the **dots under yeh/beh/teh/noon were 1px each**, so the marks
that tell those letters apart were the faintest ink on the screen. Widening runs
in **x only** is deliberate: it is what `setEmbolden` does, whereas thickening in
y merges harakat into their letters and was tried on the glass and reverted.
