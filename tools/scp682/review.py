"""Read the compiled Studio model back and compare with the unused animation camera."""
import hashlib
import json
import struct
import sys
from PIL import Image,ImageDraw,ImageFont
from export import AXES, UNITS
from views import *
from foley import EVENTS
from studio import Model, Surface, accumulate, matrices


class Compiled:
    def __init__(self,path):
        self.model=m=Model(path)
        self.surface=Surface(m)
        self.g=m.studio
        self.motion=[accumulate(matrices(m.raw(n)),m.parents) for n in CLIPS]
        self.textures=[]
        for t in self.g.textures:
            w,h=t['width'],t['height']
            a=np.frombuffer(t['pixels'],np.uint8)
            self.textures.append(a[w*h:].reshape(256,3)[a[:w*h].reshape(h,w)])
        self.f,self.uv,self.mat=[],[],[]
        off=0
        for part in self.g.bodyparts:
            for sub in part['models']:
                for mesh in sub['meshes']:
                    corners=np.array(mesh['triangles'],int)
                    material=self.g.skins[0][mesh['skinref']]
                    size=np.array(self.textures[material].shape[1::-1])
                    self.f.extend(corners[:,:,0]+off)
                    self.uv.extend((corners[:,:,2:]+.5)/size)
                    self.mat.extend([material]*len(corners))
                off+=len(sub['vertices'])
        self.f,self.uv,self.mat=map(np.array,(self.f,self.uv,self.mat))

    def points(self,k,j):
        return self.surface.skin(self.motion[k][j])@AXES/UNITS

    def render(self,k,j,cam):
        return shaded(self.points(k,j),self.f,self.uv,self.mat,self.textures,cam)


def font(size):
    for entry in ('C:/Windows/Fonts/msyh.ttc','C:/Windows/Fonts/simhei.ttf','DejaVuSans.ttf'):
        try: return ImageFont.truetype(entry,size)
        except OSError: pass
    return ImageFont.load_default()


def review():
    path=WORK/'compile/scp682.mdl'
    model=Compiled(path)
    cam=Camera.read(MOTION/'v5.json')
    results={}
    chosen=(4,8,14,14,20)
    table=Image.new('RGB',(1080,1260),'#282b2c')
    draw=ImageDraw.Draw(table)
    for x,text in [(34,'网页参考'),(574,'GoldSrc MDL')]:
        draw.text((x,14),text,font=font(25),fill='#ecece7')
    for k,name in enumerate(CLIPS):
        seq=model.model.seq[name]
        count=model.g.i(seq+56)
        assert count==INTERVALS[k]+1 and model.g.f(seq+32,1)[0]==24
        assert bool(model.g.i(seq+36)&1)==(k in (0,1))
        cues=[]
        for i in range(model.g.i(seq+48)):
            off=model.g.i(seq+52)+76*i
            frame,code=struct.unpack_from('<ii',model.g.data,off)
            sample=model.g.string(off+12,64)
            cues.append((frame,code,sample))
        assert cues==[(fr,c,'scp682/'+s+'.wav') for fr,c,s in EVENTS[k]],cues
        if k<2:
            assert np.max(np.abs(model.points(k,0)-model.points(k,count-1)))<.002
        vals=[]
        for j in range(count):
            pic,hit=model.render(k,j,cam)
            ref=rgb_file(MOTION/f'm_v5_{k}_{j}.png')
            obs=foreground(ref).astype(bool)
            overlap=float((obs&hit).sum()/(obs|hit).sum())
            vals.append(overlap)
            if j==chosen[k]:
                ref[~obs]=[54,55,57]
                ys,xs=np.where(hit|obs)
                box=(xs.min()-8,ys.min()-8,xs.max()+9,ys.max()+9)
                top=65+238*k
                zh=['奔跑','待机','右侧攻击','左侧攻击','死亡'][k]
                draw.text((34,top),f'{zh}   第 {j} 帧',font=font(18),fill='#c6cfbc')
                for col,img in enumerate([ref,pic]):
                    crop=Image.fromarray(img).crop(box)
                    scale=min(504/crop.width,191/crop.height)
                    crop=crop.resize((round(crop.width*scale),round(crop.height*scale)),Image.Resampling.LANCZOS)
                    table.paste(crop,(34+540*col,top+34))
        results[name]=dict(frames=count,duration=(count-1)/24,mean=float(np.mean(vals)),
                           worst=float(min(vals)),worst_frame=int(np.argmin(vals)),values=vals)
    table.save(WORK/'comparison.jpg',quality=95)
    report=dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),bytes=path.stat().st_size,
                triangles=len(model.f),vertices=len(model.surface.vertices),bones=len(model.model.names),
                embedded_textures=[list(tx.shape[:2]) for tx in model.textures],hitboxes=model.g.i(156),
                events=sum(len(c) for c in EVENTS),mean_over_all_frames=float(np.mean([v for t in results.values() for v in t['values']])),
                held_out_camera='v5',clips=results)
    (WORK/'model-review.json').write_text(json.dumps(report,indent=2))
    print('mean over all frames',report['mean_over_all_frames'])
    for name,t in results.items(): print(name,'mean',round(t['mean'],4),'worst',round(t['worst'],4))


if __name__=='__main__':
    review()
