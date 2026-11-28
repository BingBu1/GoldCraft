// Capture visible, calibrated views through the public Sketchfab Viewer API.
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const { chromium } = require('playwright');

const asset = '256d948a60fa418c934cee3f4aa1caf7';
const width = 896, height = 672;
const area = path.resolve(__dirname, '../../build/scp682/reference');
const kind = process.argv[2];
const mode = process.argv[3];
const kinds = new Set(['capture', 'open', 'face', 'albedo', 'albedo-face', 'markers']);
if (!kinds.has(kind) || !['rest', 'motion'].includes(mode)) {
    throw Error('Usage: node capture.cjs <capture|open|face|albedo|albedo-face|markers> <rest|motion>');
}
const output = path.join(area, kind);
const close = kind === 'face' || kind === 'albedo-face';
const target = close ? [.28, 1.9, .98] : [0, -.38, .85];
const radius = close ? 4.7 : 12.7;
const cameras = [[0,7], [89,9], [178,12], [265,16], [8,72], [42,30], [-45,-20],
    [40,2], [129,-10], [212,-15], [318,34], [76,50], [141,46], [221,43], [268,-52],
    [19,-45], [180,67]].map((angle, i) => ['v' + i, ...angle]);

function bindViews() {
    const views = close ? [] : cameras.slice(0,kind === 'markers' ? 17 : 7);
    for (let i=0; i<14; ++i) views.push(['ring'+i, 360*i/14, 2]);
    for (let i=0; i<12; ++i) views.push(['high'+i, 360*i/12+8, 40]);
    for (let i=0; i<8; ++i) views.push(['low'+i, i*45+12, -32]);
    views.push(['under', -28, -79]);
    if (!close) views.push(['holdside',29,13], ['holdfront',139,27], ['holdhigh',237,58]);
    return views;
}

function calibrationChart() {
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = 1024;
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = '#252525';
    ctx.fillRect(0, 0, 1024, 1024);
    const colors = Array.from({length:1000}, (_, i) => i);
    let random = 682;
    for (let i=colors.length-1; i>0; --i) {
        random = (Math.imul(random,1664525)+1013904223) >>> 0;
        const j = random % (i+1);
        [colors[i],colors[j]] = [colors[j],colors[i]];
    }
    for (let i=0; i<24*24; ++i) {
        const code = colors[i];
        const channels = [code%10, Math.floor(code/10)%10, Math.floor(code/100)];
        ctx.fillStyle = 'rgb(' + channels.map(v => 30+21*v).join(',') + ')';
        ctx.beginPath();
        ctx.arc(((i%24)+.5)*1024/24, (Math.floor(i/24)+.5)*1024/24, 16, 0, 2*Math.PI);
        ctx.fill();
    }
    return canvas.toDataURL('image/png');
}

const html = `<!doctype html><style>*{margin:0}body{overflow:hidden}
iframe{display:block;border:0;width:100vw;height:100vh}</style>
<iframe allow="autoplay; fullscreen"></iframe>
<script src="https://static.sketchfab.com/api/sketchfab-viewer-1.12.1.js"></script>
<script>new Sketchfab(document.querySelector('iframe')).init('${asset}', {
    autostart:1, preload:1, autospin:0, animation_autoplay:0, ui_infos:0, ui_controls:0,
    dnt:1, success:api=>{window.viewer=api;api.addEventListener('viewerready',()=>window.ready=true);api.start()}
});</script>`;

async function run() {
    fs.mkdirSync(output, {recursive:true});
    const server = http.createServer((req,res) => {
        res.setHeader('Content-Type','text/html');
        res.end(html);
    });
    await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
    let browser;
    try {
        browser = await chromium.launch({
            headless:true, executablePath:process.env.SCP682_CHROMIUM || undefined,
            proxy:process.env.SCP682_PROXY ? {server:process.env.SCP682_PROXY,bypass:'127.0.0.1,localhost'} : undefined,
            args:['--autoplay-policy=no-user-gesture-required','--enable-unsafe-swiftshader']
        });
        const page = await browser.newPage({viewport:{width,height}});
        await page.goto('http://127.0.0.1:'+server.address().port);
        await page.waitForFunction(() => window.ready, undefined, {timeout:100000});
        const call = (method,...args) => page.evaluate(([name,parameters]) => new Promise((resolve,reject) =>
            window.viewer[name](...parameters,(err,data) => err ? reject(String(err)) : resolve(data))),[method,args]);
        await call('setBackground',{color:[0,1,0]});
        if (kind==='markers') {
            const texture = await call('addTexture',await page.evaluate(calibrationChart));
            for (const material of await call('getMaterialList')) {
                for (const name of ['AlbedoPBR','DiffuseColor']) {
                    const channel = material.channels[name];
                    if (channel) Object.assign(channel,{enable:true,factor:1,color:[1,1,1],texture:{uid:texture}});
                }
                await call('setMaterial',material);
            }
        }
        if (kind.startsWith('albedo') || kind==='markers') {
            await call('setShadingStyle','pbr',{type:'shadeless'});
            await page.waitForTimeout(600);
        }
        const clips = await call('getAnimations');
        fs.writeFileSync(path.join(area,'viewer.json'),JSON.stringify({animations:clips},null,2));
        const select = async k => { await call('setCurrentAnimationByUID',clips[k][0]); await call('pause'); };
        const seek = async t => { await call('seekTo',t); await page.waitForTimeout(52); };
        async function capture(stem) {
            const file = path.join(output,stem+'.png');
            if (fs.existsSync(file)) return;
            const url = await call('getScreenShot',width,height,'image/png');
            fs.writeFileSync(file,Buffer.from(url.split(',')[1],'base64'));
        }
        const chosen = mode === 'rest' ? bindViews() : cameras;
        if (mode==='rest') {
            await select(1);
            await seek(kind==='capture' ? 0 : 8/24);
        }
        for (const [label,az,el] of chosen) {
            const a=az*Math.PI/180, b=el*Math.PI/180;
            const pos=[Math.cos(b)*Math.cos(a),Math.cos(b)*Math.sin(a),Math.sin(b)].map((v,i)=>target[i]+radius*v);
            await call('setCameraLookAt',pos,target,.01);
            await page.waitForTimeout(1050);
            const probe=[[0,0,0],[1,0,0],[0,1,0],[0,0,1]];
            const pixels=await page.evaluate(pts=>Promise.all(pts.map(p=>new Promise(resolve=>
                viewer.getWorldToScreenCoordinates(p,v=>resolve([v.canvasCoord[0],v.canvasCoord[1]]))))),probe);
            const stem = mode==='rest' ? 'rest_'+label : label;
            const record = {pose:await call('getCameraLookAt'),fov:await call('getFov'),probe,pixels,W:width,H:height};
            fs.writeFileSync(path.join(output,stem+'.json'),JSON.stringify(record));
            if (mode==='rest') {
                await capture(stem);
            } else {
                for (let k=0; k<clips.length; ++k) {
                    await select(k);
                    for (let j=0; j<=Math.round(clips[k][2]*24); ++j) {
                        const frame='m_'+label+'_'+k+'_'+j;
                        if (fs.existsSync(path.join(output,frame+'.png'))) continue;
                        await seek(Math.min(clips[k][2]-.00025,j/24));
                        await capture(frame);
                    }
                }
            }
        }
    } finally {
        if (browser) await browser.close();
        server.close();
    }
}
run().catch(e => {console.error(e.message);process.exitCode=1;});
