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
python3 gen_faces.py                                 # writes out/*.c
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

**Rejected: evening out the strokes.** A pass that widened every 1px-wide stroke
to 2px was built and shipped as a fourth face in 1.16.0, on the reasoning that
this panel needs two adjacent dots to read black - and the dots under
yeh/beh/teh/noon were 1px each. It measured well (115 such pixels down to 18 for
0.9% more ink) and Fouad rejected it on the glass anyway: not worth a face.
Removed after 1.16.0. Don't rebuild it expecting a different answer; if
jaggedness comes up again the levers are hinting (off) and size, above.
