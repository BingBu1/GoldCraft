"""Assemble the tested local asset, including all model-referenced samples, then read back the ZIP."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from tempfile import NamedTemporaryFile
import wave
import zipfile
from foley import EVENTS,SOURCES
from views import ROOT,WORK


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def publish_file(source,dest):
    dest.parent.mkdir(parents=True,exist_ok=True)
    if dest.is_file() and digest(source)==digest(dest):
        return
    with NamedTemporaryFile(dir=dest.parent,delete=False) as temp:
        staging=Path(temp.name)
    try:
        shutil.copyfile(source,staging)
        staging.replace(dest)
    finally:
        staging.unlink(missing_ok=True)


def main():
    stage=WORK/'compile'
    dist=ROOT/'dist/scp682'
    dist.mkdir(parents=True,exist_ok=True)
    model=stage/'scp682.mdl'
    review=json.loads((WORK/'model-review.json').read_text())
    if review['sha256']!=digest(model) or (WORK/'engine-validation.log').read_text().splitlines()[-1]!='PASS':
        raise ValueError('Review or isolated engine verification missing')
    checked=WORK/'engine-run/checkmdl/models/scp682/scp682.mdl'
    if digest(checked)!=digest(model):
        raise ValueError('Engine-tested MDL differs from the package')
    motion=json.loads((WORK/'motion-repair/final.json').read_text())
    if motion['model']!=digest(model) or motion['substeps']<16 or not motion['passed']:
        raise ValueError('Motion check does not match the model')
    video=WORK/'SCP-682-Motion.mp4'
    preview=json.loads((WORK/'preview-receipt.json').read_text())
    if preview['model']!=digest(model) or preview['video']!=digest(video):
        raise ValueError('Preview does not match the model')
    raw=subprocess.check_output(['ffprobe','-v','error','-show_entries',
                                 'stream=codec_type,codec_name,width,height,nb_frames,duration,avg_frame_rate',
                                 '-of','json',str(video)])
    streams=json.loads(raw)['streams']
    assert any(t['codec_name']=='h264' and int(t['nb_frames'])==606 and
               t['avg_frame_rate']=='48/1' for t in streams)
    assert any(t['codec_name']=='aac' and float(t['duration'])>12.5 for t in streams)
    files={'cstrike/models/scp682/scp682.mdl':model}
    for cues in EVENTS:
        for _,_,name in cues:
            path=stage/f'sound/scp682/{name}.wav'
            with wave.open(str(path),'rb') as w:
                assert (w.getnchannels(),w.getframerate(),w.getsampwidth(),w.getcomptype())==(1,22050,2,'NONE')
                assert w.getnframes()>0
            if digest(path)!=digest(WORK/f'engine-run/checkmdl/sound/scp682/{name}.wav'):
                raise ValueError(f'Engine-tested sample differs: {name}')
            files[f'cstrike/sound/scp682/{name}.wav']=path
    credits=WORK/'CREDITS.txt'
    text=['SCP-682 | GoldSrc Studio v10','',
          '解压后把 cstrike 合并到 CS 游戏目录。怪物实体使用 models/scp682/scp682.mdl。',
          '声音文件位于 sound/scp682/。NPC 或插件应预缓存这十个声音，并派发 Studio 怪物声音事件 1004 和 1008。',
          '五段序列依网页顺序为奔跑、待机、右侧攻击、左侧攻击、死亡。奔跑和待机支持循环。',
          '模型按每个网页场景单位 39.37 个 GoldSrc 单位制作。实际摆放可由地图或插件调整。','',
          '网页模型与动画参考为 shuripusta 的 SCP-682 Reptile。',
          'https://sketchfab.com/3d-models/scp-682-reptile-256d948a60fa418c934cee3f4aa1caf7',
          '本包的网格、骨架和动画通过公开渲染画面重建。外观包含按多视角画面烘焙的纹理。',
          'SCP-682 概念来源于 Dr Gears 的 SCP Foundation 文章。','https://scp-wiki.wikidot.com/scp-682',
          '文章使用 CC BY-SA 3.0。网页委托模型的商用许可需向权利人确认。','',
          '本包的配音由下列 CC0 动物叫声、牙齿和地面拟音重新剪辑混合。']
    text += [f'{author} — {title}\n{url}' for author,title,url in SOURCES.values()]
    credits.write_text('\n'.join(text)+'\n',encoding='utf-8-sig')
    files['CREDITS.txt']=credits
    for member,path in files.items():
        publish_file(path,dist/member)
    final=dist/'SCP-682-GoldSrc.zip'
    with NamedTemporaryFile(dir=dist,delete=False) as fp:
        tmp=Path(fp.name)
    try:
        with zipfile.ZipFile(tmp,'w',zipfile.ZIP_DEFLATED,compresslevel=8) as z:
            for name,path in sorted(files.items()):
                entry=zipfile.ZipInfo(name,date_time=(2026,11,28,0,0,0))
                entry.compress_type=zipfile.ZIP_DEFLATED
                entry.external_attr=0o100644<<16
                z.writestr(entry,path.read_bytes(),compresslevel=8)
        with zipfile.ZipFile(tmp) as z:
            assert set(z.namelist())==set(files) and z.testzip() is None
            for name,path in files.items():
                assert hashlib.sha256(z.read(name)).hexdigest()==digest(path)
        tmp.replace(final)
    finally:
        tmp.unlink(missing_ok=True)
    publish_file(video,dist/video.name)
    publish_file(WORK/'comparison.jpg',dist/'SCP-682-Comparison.jpg')
    report={'zip':final.name,'sha256':digest(final),'size':final.stat().st_size,'model':review,'streams':streams,
            'contents':{n:digest(p) for n,p in sorted(files.items())}}
    (WORK/'release.json').write_text(json.dumps(report,indent=2))
    print(final.name,final.stat().st_size,digest(final))


if __name__=='__main__':
    main()
