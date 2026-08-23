"""Build the editor's Arabic faces as u8g2 arrays.

Paragraph height (bytes 15/16 of the u8g2 header) sets the line pitch, so a
replacement face must match the one it replaces: 19 for _m, 22 for _l.
"""
import codecs, os, subprocess

TTF = os.path.expanduser('~/Library/Fonts/IBMPlexSansArabic-Bold.ttf')
BDFCONV = 'u8g2/tools/font/bdfconv/bdfconv'
# bdfconv wants $ for hex. The ASCII range is not optional: bdfconv takes the
# header's ascent/descent from it, and an Arabic-only build reports paragraph
# height 0, which leaves the renderer with no pitch.
MAP = '32-126,$600-$6ff,$fe70-$feff'
SUBSET = '32_126 1536_1791 65136_65279'

FACES = [
    # name,                            px, hinted
    ('u8g2_font_plexclean_arabic_m',   21, False),
    ('u8g2_font_plexclean_arabic_l',   25, False),
    ('u8g2_font_plexbig_arabic_m',     23, True),
    ('u8g2_font_plexbig_arabic_l',     28, True),
]

def header(path):
    src = open(path, encoding='latin-1').read()
    body = src[src.index('=') + 1:src.rindex(';')]
    rows = [l.strip()[1:-1] for l in body.splitlines() if l.strip().startswith('"')]
    d = codecs.escape_decode(''.join(rows).encode('latin-1'))[0]
    desc = d[16] - 256 if d[16] > 127 else d[16]
    return dict(para=d[15] - desc, max_w=d[9], max_h=d[10], size=len(d))

def build(name, px, hinted, out):
    bdf = f'/tmp/{name}.bdf'
    cmd = ['otf2bdf', '-p', str(px), '-r', '72', '-l', SUBSET]
    if not hinted:
        cmd.append('-n')          # hinting grid-fits stems and sheds stray
    cmd += ['-o', bdf, TTF]       # single pixels on Arabic curves
    subprocess.run(cmd, capture_output=True, check=True)
    subprocess.run([BDFCONV, '-f', '1', '-m', MAP, '-n', name, '-o', out, bdf],
                   capture_output=True, check=True)
    h = header(out)
    print(f"{name}: px={px} hinted={int(hinted)} para={h['para']} "
          f"max={h['max_w']}x{h['max_h']} bytes={h['size']}")

if __name__ == '__main__':
    os.makedirs('out', exist_ok=True)
    for name, px, hinted in FACES:
        build(name, px, hinted, f"out/{name}.c")
