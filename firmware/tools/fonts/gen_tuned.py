import subprocess, os, bdf, tune
BDFCONV='u8g2/tools/font/bdfconv/bdfconv'
MAP='32-126,$600-$6ff,$fe70-$feff'
def build(px, name, out):
    src=bdf.make_base(px=px, hint=False, out=f'/tmp/t_{px}.bdf')
    head,gs,tail=bdf.load(src)
    w,h,touched=tune.tune(gs)
    tuned=f'/tmp/t_{px}_tuned.bdf'
    bdf.save(tuned, head, gs, tail)
    subprocess.run([BDFCONV,'-f','1','-m',MAP,'-n',name,'-o',out,tuned],
                   capture_output=True, check=True)
    size=len(open(out).read())
    print(f"{name}: px={px} widened={w} pinholes={h} glyphs_touched={touched} file={size}B")
os.makedirs('faces3', exist_ok=True)
build(21, 'u8g2_font_plextuned_arabic_m', 'faces3/plextuned_m.c')
build(25, 'u8g2_font_plextuned_arabic_l', 'faces3/plextuned_l.c')
