"""Reference compositor: decodes a packed clip exactly like the Player shader
(BT.709 limited -> RGB, alpha = (Y-16)/219, premultiplied OVER) onto a
procedural background. Used to validate the format end-to-end without Windows."""
import sys, json, subprocess, numpy as np
from PIL import Image
mkv, clipjson, out = sys.argv[1], sys.argv[2], sys.argv[3]
c = json.load(open(clipjson)); W, H = c["width"], c["height"]; ay = c["alpha_rect"][1]
FH = ay + H
p = subprocess.Popen(["ffmpeg","-v","error","-i",mkv,"-f","rawvideo","-pix_fmt","yuv420p","-"],stdout=subprocess.PIPE)
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
bg = np.stack([40+30*xx/W, 30+20*yy/H, 70+60*(1-yy/H)], -1)                 # procedural gradient
bg += 18*((((xx//24)+(yy//24))%2)[...,None])                                 # checker to expose alpha
def yuv2rgb(Y,U,V):
    Y=(Y-16)*(255/219.); U=(U-128)*(255/224.); V=(V-128)*(255/224.)
    return np.stack([Y+1.5748*V, Y-0.1873*U-0.4681*V, Y+1.8556*U],-1)
w = subprocess.Popen(["ffmpeg","-v","error","-y","-f","rawvideo","-pix_fmt","rgb24","-s",f"{W}x{H}","-framerate",c["fps"],
                      "-i","-","-c:v","libx264","-crf","20","-pix_fmt","yuv420p",out],stdin=subprocess.PIPE)
n=0; stills=[]
while True:
    b=p.stdout.read(W*FH*3//2)
    if len(b)<W*FH*3//2: break
    f=np.frombuffer(b,np.uint8); Y=f[:W*FH].reshape(FH,W).astype(np.float32)
    U=f[W*FH:W*FH*5//4].reshape(FH//2,W//2).repeat(2,0).repeat(2,1).astype(np.float32)
    V=f[W*FH*5//4:].reshape(FH//2,W//2).repeat(2,0).repeat(2,1).astype(np.float32)
    rgb=np.clip(yuv2rgb(Y[:H],U[:H],V[:H]),0,255); a=np.clip((Y[ay:ay+H]-16)/219.,0,1)[...,None]
    o=(rgb*a+bg*(1-a)).clip(0,255).astype(np.uint8); w.stdin.write(o.tobytes())
    if n in (0,60,120,220): stills.append(o)
    n+=1
w.stdin.close(); w.wait(); print("frames",n)
Image.fromarray(np.concatenate(stills,1)).save(out.replace(".mp4",".png"))
