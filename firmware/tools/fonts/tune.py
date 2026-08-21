"""Even out stroke weight in a BDF so nothing renders 1 dot wide.

This panel needs two adjacent dots to go properly black - a single dot reads
grey. An automatic rasteriser leaves 1px-wide fragments all over the Arabic
forms (yeh, kaf, ain, heh, noon, meem), and each one of those is a stroke the
eye sees as faint or broken. Widening them to 2px in x is the same operation
Bold applies globally (setEmbolden), but applied only where the stroke is
actually too thin, so Normal keeps its weight everywhere else.

x only, deliberately: thickening in y merges harakat into their letters and was
tried on the glass and reverted (see CLAUDE.md). Marks are skipped outright.
"""
MARKS = set(range(0x064B, 0x0653)) | set(range(0xFE70, 0xFE80))

def widen_thin_runs(bits, w, h):
    """every horizontal run of ink exactly 1px wide becomes 2px, extending left
       (the direction setEmbolden uses, so the two compose predictably)"""
    out=[row[:] for row in bits]
    n=0
    for y in range(h):
        x=0
        while x < w:
            if bits[y][x]:
                x0=x
                while x < w and bits[y][x]: x+=1
                if x-x0 == 1:                      # a 1px-wide run
                    if x0-1 >= 0 and not out[y][x0-1]:
                        out[y][x0-1]=1; n+=1
                    elif x0+1 < w and not out[y][x0+1]:
                        out[y][x0+1]=1; n+=1
            else:
                x+=1
    return out, n

def close_pinholes(bits, w, h):
    """a 1px gap with ink on both sides *and* above-or-below is a rasteriser
       pinhole in a stroke, not a counter: fill it"""
    out=[row[:] for row in bits]
    n=0
    for y in range(h):
        for x in range(1, w-1):
            if bits[y][x] or not (bits[y][x-1] and bits[y][x+1]):
                continue
            up   = bits[y-1][x] if y > 0 else 0
            down = bits[y+1][x] if y < h-1 else 0
            if up and down:            # enclosed on all four sides
                out[y][x]=1; n+=1
    return out, n

def tune(glyphs):
    widened=holes=touched=0
    for g in glyphs:
        if g.enc in MARKS or g.w == 0 or g.h == 0:
            continue
        b,n1 = widen_thin_runs(g.bits, g.w, g.h)
        b,n2 = close_pinholes(b, g.w, g.h)
        if n1 or n2:
            g.bits = b; touched += 1
        widened += n1; holes += n2
    return widened, holes, touched
