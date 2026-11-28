"""Render an audiovisual showcase from the final MDL and the exact packaged WAVs."""
import math
import subprocess
import wave
from PIL import Image,ImageDraw
from review import Compiled,font
from foley import EVENTS
from views import *

FPS=24
RATE=22050
W,H=1120,680


def camera(k,phase):
    az=[25,52,43,146,61][k]+(phase-.5)*8
    el=[14,16,18,15,20][k]
    radius=[9.35,8.85,10.25,10.75,9.75][k]
    target=np.array([.04,-.37,[.73,.78,1.23,1.16,.71][k]])
    a,e=map(math.radians,(az,el))
    pos=target+radius*np.array([math.cos(a)*math.cos(e),math.sin(a)*math.cos(e),math.sin(e)])
    return Camera(dict(W=W,H=H,fov=30,pose=dict(position=pos,target=target)))


def preview():
    model=Compiled(WORK/'compile/scp682.mdl')
    shotlist=[(1,2),(0,5),(2,2),(3,2),(4,1)]
    sequence=[]
    cues=[]
    for k,repeat in shotlist:
        length=INTERVALS[k]
        for r in range(repeat):
            start=len(sequence)
            for frame,channel,sample in EVENTS[k]:
                cues.append((start+frame,channel,sample))
            sequence.extend((k,j,(r*length+j)/max(1,repeat*length-1)) for j in range(length))
        if k==4:
            sequence.extend((4,length,1.) for _ in range(24))
    sound=np.zeros(math.ceil(RATE*len(sequence)/FPS))
    for index,(j,channel,sample) in enumerate(cues):
        path=WORK/f'compile/sound/scp682/{sample}.wav'
        with wave.open(str(path),'rb') as w:
            assert w.getparams()[:3]==(1,2,RATE)
            pcm=np.frombuffer(w.readframes(w.getnframes()),'<i2')/32768
        onset=round(j*RATE/FPS)
        next_event=min((round(next_j*RATE/FPS) for next_j,ch,_ in cues[index+1:] if ch==channel),default=len(sound))
        length=min(len(pcm),len(sound)-onset,next_event-onset)
        sound[onset:onset+length]+=pcm[:length]*.83
    with wave.open(str(WORK/'preview-audio.wav'),'wb') as w:
        w.setparams((1,2,RATE,0,'NONE',''))
        w.writeframes(np.rint(np.clip(sound,-.97,.97)*32767).astype('<i2').tobytes())
    cmd=['ffmpeg','-v','error','-y','-f','rawvideo','-pix_fmt','rgb24','-s',f'{W}x{H}','-r',str(FPS),
         '-i','-','-i',str(WORK/'preview-audio.wav'),'-map','0:v:0','-map','1:a:0','-c:v','libx264',
         '-preset','fast','-crf','17','-pix_fmt','yuv420p','-c:a','aac','-b:a','112k',
         '-movflags','+faststart',str(WORK/'SCP-682-Motion.mp4')]
    proc=subprocess.Popen(cmd,stdin=subprocess.PIPE)
    yy,xx=np.mgrid[0:H,0:W]
    falloff=np.exp(-2.1*((xx/W-.5)**2+(yy/H-.54)**2))
    ground=np.stack([23+36*falloff,28+37*falloff,29+36*falloff],axis=2)
    small,large=font(21),font(31)
    for k,j,phase in sequence:
        cam=camera(k,phase)
        xyz=model.points(k,j)
        shadow=xyz.copy()
        shadow[:,:2]+=shadow[:,2,None]*[-.44,-.21]
        shadow[:,2]=0
        # Reduce shadow resolution; the model itself is rendered at full resolution.
        projected=cam.project(shadow)
        projected[:,:2]*=.5
        hidden=buffer(projected,model.f,W//2,H//2)[0]>=0
        soft=cv2.GaussianBlur(hidden.astype(np.float32),(0,0),4.8)
        plate=ground*(1-.38*cv2.resize(soft,(W,H))[:,:,None])
        colors,hit=model.render(k,j,cam)
        plate[hit]=colors[hit]
        im=Image.fromarray(plate.astype(np.uint8))
        pen=ImageDraw.Draw(im)
        pen.text((44,32),'SCP-682',font=large,fill=(226,230,220))
        pen.text((44,H-61),['奔跑','待机','右侧攻击','左侧攻击','死亡'][k],font=small,fill=(216,223,213))
        proc.stdin.write(im.tobytes())
    proc.stdin.close()
    if proc.wait()!=0: raise RuntimeError('Preview encode failed')
    print('preview frames',len(sequence),'seconds',len(sequence)/FPS)


if __name__=='__main__':
    preview()
