# beforeafter.py <tag> <picture> <x> <y> <w> <h> [crop fx,fy,fw,fh of the visible picture]
# Before: the 2560 px mipmapped texture drawn at the picture's rect (what 0.2.0 shows).
# After: the same, with the texture Lumen now cuts for the settled view laid over it.
# Both through real GL (EGL surfaceless); compared with a Lanczos resample of the file at that rect.
import sys, subprocess, os, numpy as np
from PIL import Image
D=os.path.dirname(os.path.abspath(__file__))+"/"
tag,pic=sys.argv[1],sys.argv[2]; x,y,w,h=[float(v) for v in sys.argv[3:7]]
VW,VH=1600,900
env=dict(os.environ); env.pop("DISPLAY",None); env.pop("WAYLAND_DISPLAY",None); env["QT_QPA_PLATFORM"]="offscreen"
if os.environ.get("SOFT","1")=="1": env["LIBGL_ALWAYS_SOFTWARE"]="1"
TURN=os.environ.get("TURN","").split()
o=subprocess.run([D+"cut",pic,str(x),str(y),str(w),str(h),str(VW),str(VH),D]+TURN,env=env,capture_output=True,text=True,check=True).stdout.split()
bw,bh,valid,sw,sh=[int(v) for v in o[:5]]; dx,dy,dw,dh=[float(v) for v in o[5:9]]
def gl(texfile,tw,th,rx,ry,rw,rh,mode):
    r=subprocess.run([D+"gltex",texfile,str(tw),str(th),D+"o.rgba",str(VW),str(VH),str(rx),str(ry),str(rw),str(rh),mode],env=env,capture_output=True,text=True)
    assert r.returncode==0, r.stderr
    gl.renderer=r.stderr.strip()
    return Image.frombytes("RGBA",(VW,VH),open(D+"o.rgba","rb").read()).convert("RGB")
before=gl(D+"base.rgba",bw,bh,x,y,w,h,"mip")
after=before.copy()
if valid:
    sharp=gl(D+"sharp.rgba",sw,sh,dx,dy,dw,dh,"nomip")
    box=(int(round(dx)),int(round(dy)),int(round(dx+dw)),int(round(dy+dh)))
    after.paste(sharp.crop(box),box)
src=Image.open(pic).convert("RGB")
if TURN:
    rot=int(TURN[0]); cx,cy,cw,ch=[float(v) for v in TURN[1:5]]
    if rot: src=src.rotate(-rot,expand=True)
    src=src.crop((round(cx*src.width),round(cy*src.height),round(cx*src.width)+max(1,round(cw*src.width)),round(cy*src.height)+max(1,round(ch*src.height))))
X0,Y0,X1,Y1=max(0,int(np.ceil(x))+1),max(0,int(np.ceil(y))+1),min(VW,int(np.floor(x+w))-1),min(VH,int(np.floor(y+h))-1)
def ideal_at(x,y,w,h):
    sx,sy=src.width/w,src.height/h
    return src.resize((X1-X0,Y1-Y0),Image.LANCZOS,box=((X0-x)*sx,(Y0-y)*sy,(X1-x)*sx,(Y1-y)*sy))
ideal=ideal_at(x,y,w,h)
# Shrunk, the sharp texture sits on whole pixels: the picture has moved by up to half a pixel, and
# is judged against the file resampled to where it now is.
snapped=(round(x),round(y),round(x+w)-round(x),round(y+h)-round(y)) if valid and w<src.width else (x,y,w,h)
ideal_after=ideal_at(*snapped)
def score(img,ref):
    a=np.asarray(ref).astype(float); b=np.asarray(img.crop((X0,Y0,X1,Y1))).astype(float); return 10*np.log10(255**2/((a-b)**2).mean()), np.abs(np.diff(b,axis=1)).mean()/np.abs(np.diff(a,axis=1)).mean()
pb,cb=score(before,ideal); pa,ca=score(after,ideal_after)
print(f"{tag}: {os.path.basename(pic)} {src.width}x{src.height} shown {w:.0f}x{h:.0f} px ({w/src.width:.2f}x) | sharp texture {sw}x{sh} | before PSNR {pb:.1f} dB, detail {cb*100:.0f}% | after PSNR {pa:.1f} dB, detail {ca*100:.0f}% | {gl.renderer.split('|')[0]}")
fx,fy,fw,fh=[float(v) for v in (sys.argv[7] if len(sys.argv)>7 else "0,0,1,1").split(",")]
cbx=(int(fx*ideal.width),int(fy*ideal.height),int((fx+fw)*ideal.width),int((fy+fh)*ideal.height))
tiles=[t.crop((X0,Y0,X1,Y1)).crop(cbx) for t in (before,after)]+[ideal_after.crop(cbx)]
k=max(1,min(4,int(500/tiles[0].width)))
out=Image.new("RGB",(sum(t.width*k+8 for t in tiles)-8,tiles[0].height*k),(255,0,255)); px=0
for t in tiles: out.paste(t.resize((t.width*k,t.height*k),Image.NEAREST),(px,0)); px+=t.width*k+8
out.save(D+os.environ.get("OUT","")+tag+"-before-after-ideal.png")
