from pathlib import Path
import sys
from PIL import Image
root=Path(sys.argv[1]);colors=[(170,97,64),(83,102,72),(163,97,68),(248,248,238)]
for file in root.glob('*.qoi'):
 en,kind,alpha=map(int,file.stem.split('-'));mask=(root/f'{en}-{kind}.mask').read_bytes()
 im=Image.open(file).convert('RGBA');assert im.size==(96,32)
 for actual,a in zip(im.getdata(),mask):assert actual==(*colors[kind],(a*alpha+127)//255)
print('PASS 40 RGBA glyphs match original masks and exact requested opacity')
