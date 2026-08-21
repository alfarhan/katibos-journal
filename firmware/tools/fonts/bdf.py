"""Read/inspect/write BDF glyph bitmaps."""
import subprocess, os

def make_base(px=21, hint=False, out='/tmp/base.bdf'):
    ttf=os.path.expanduser('~/Library/Fonts/IBMPlexSansArabic-Bold.ttf')
    cmd=['otf2bdf','-p',str(px),'-r','72','-l','32_126 1536_1791 65136_65279']
    if not hint: cmd.append('-n')
    cmd+=['-o',out,ttf]
    subprocess.run(cmd,capture_output=True,check=True)
    return out

class Glyph:
    def __init__(self, name, enc, dwidth, bbx, rows):
        self.name, self.enc, self.dwidth = name, enc, dwidth
        self.w, self.h, self.xoff, self.yoff = bbx
        self.bits = [[(int(r,16) >> (len(r)*4-1-x)) & 1 if x < len(r)*4 else 0
                      for x in range(self.w)] for r in rows]
    def hexrows(self):
        pad = ((self.w + 7)//8)*2
        out=[]
        for row in self.bits:
            v=0
            for x,b in enumerate(row):
                if b: v |= 1 << (self.w-1-x)
            v <<= (pad*4 - self.w)
            out.append(f'{v:0{pad}X}')
        return out
    def art(self):
        return '\n'.join(''.join('#' if b else '.' for b in r) for r in self.bits)

def load(path):
    head=[]; glyphs=[]; tail=[]
    cur=None; rows=None; name=enc=dw=None; bbx=None; phase=0
    for line in open(path, errors='replace'):
        t=line.split()
        if phase==0:
            if t and t[0]=='STARTCHAR': phase=1
            else:
                head.append(line); continue
        if t and t[0]=='STARTCHAR': name=' '.join(t[1:]); rows=None
        elif t and t[0]=='ENCODING': enc=int(t[1])
        elif t and t[0]=='DWIDTH': dw=(int(t[1]), int(t[2]))
        elif t and t[0]=='BBX': bbx=(int(t[1]),int(t[2]),int(t[3]),int(t[4]))
        elif t and t[0]=='BITMAP': rows=[]
        elif t and t[0]=='ENDCHAR':
            glyphs.append(Glyph(name,enc,dw,bbx,rows)); rows=None
        elif t and t[0]=='ENDFONT': tail.append(line)
        elif rows is not None: rows.append(t[0])
    return head, glyphs, tail

def save(path, head, glyphs, tail):
    with open(path,'w') as f:
        f.writelines(head)
        for g in glyphs:
            f.write(f'STARTCHAR {g.name}\n')
            f.write(f'ENCODING {g.enc}\n')
            f.write('SWIDTH 0 0\n')
            f.write(f'DWIDTH {g.dwidth[0]} {g.dwidth[1]}\n')
            f.write(f'BBX {g.w} {g.h} {g.xoff} {g.yoff}\n')
            f.write('BITMAP\n')
            for r in g.hexrows(): f.write(r+'\n')
            f.write('ENDCHAR\n')
        f.write('ENDFONT\n')
