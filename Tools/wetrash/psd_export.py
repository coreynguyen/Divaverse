import sys, os
from psd_tools import PSDImage
from PIL import Image
src, out = sys.argv[1], sys.argv[2]; os.makedirs(out, exist_ok=True)
for f in ['frames','clothes','sculpts','ties','waterjet','steeringhandle']:
    p=PSDImage.open(f'{src}/{f}.psd'); names=[]
    for l in p:
        if l.name=='Background': continue
        im=Image.new('RGBA',p.size,(0,0,0,0)); t=l.topil()
        if t is not None: im.paste(t.convert('RGBA'),(l.bbox[0],l.bbox[1]))
        n=l.name.strip().lower().replace(' ','_'); im.save(f'{out}/{f}__{n}.png'); names.append((n,l.bbox, im.getbbox()))
    print(f, len(names), [(n,b) for n,_,b in names] if f!='frames' else '')
