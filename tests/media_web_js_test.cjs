// Exercise the production script and bounded streaming against DOM/codec/network fakes.
const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const root=path.resolve(__dirname,'..'),out=path.join(root,'.tools/media-ota-20261114/js-test');fs.mkdirSync(out,{recursive:true});
const source=fs.readFileSync(path.join(root,'src/media_web_assets.h'),'utf8').split('R"SCRIPT(')[1].split(')SCRIPT"')[0].replace(/<\/?script>/g,'');
class Element extends EventTarget{constructor(){super();this.value='';this.files=[];this.hidden=false;this.disabled=false;this.textContent='';this.classList={toggle(){}};this.checked=true}removeAttribute(){} }
const elements=new Map(),el=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id)};
class Video extends Element{constructor(){super();this.duration=1.2;this.videoWidth=160;this.videoHeight=120;this.readyState=2;this.position=0}load(){queueMicrotask(()=>this.dispatchEvent(new Event('loadeddata')))}get currentTime(){return this.position}set currentTime(n){this.position=n;queueMicrotask(()=>this.dispatchEvent(new Event('seeked')))}}
class Image{set src(n){this.naturalWidth=320;this.naturalHeight=240;queueMicrotask(()=>this.onload())}}
function canvas(){const c={width:0,height:0,getContext(){return{fillStyle:'',fillRect(){},drawImage(){},getImageData(){const data=new Uint8ClampedArray(c.width*c.height*4);for(let i=0;i<data.length;i+=4){data[i]=255;data[i+3]=255}return{data}}}}};return c}
class AudioContext{async decodeAudioData(){const wave=new Float32Array(57600);for(let i=0;i<wave.length;i++)wave[i]=.5*Math.sin(i*.05);return{numberOfChannels:1,sampleRate:48000,getChannelData(){return wave}}}async close(){}}
let next=1,sessions=new Map(),saved=[],cancelled=0,cancelAt=0;
async function fetchFake(url,options={}){
 if(options.signal?.aborted)throw new DOMException('cancelled','AbortError');
 let data={ok:true};
 if(url==='/api/update/status')data={ok:true,current_version:'1.7.0',message:'ready',busy:false};
 else if(url==='/api/files/begin'){const d=JSON.parse(options.body),token=String(next++);sessions.set(token,{...d,parts:[],used:0});data={ok:true,token}}
 else if(url==='/api/files/chunk'){const token=options.headers['X-Upload-Token'],s=sessions.get(token);assert(s);assert.equal(+options.headers['X-Offset'],s.used);const part=Buffer.from(options.body);assert(part.length<=65536);s.parts.push(part);s.used+=part.length;if(cancelAt&&s.used>=cancelAt){cancelAt=0;el('mediaCancel').onclick();throw new DOMException('cancelled','AbortError')}}
 else if(url==='/api/files/finish'){const token=JSON.parse(options.body).token,s=sessions.get(token);assert.equal(s.used,s.size);const bytes=Buffer.concat(s.parts);fs.writeFileSync(path.join(out,s.name),bytes);sessions.delete(token);saved.push({kind:s.kind,name:s.name,bytes:bytes.length});data={ok:true,path:'/'+s.kind+'/'+s.name}}
 else if(url==='/api/files/cancel'){sessions.delete(JSON.parse(options.body).token);cancelled++}
 return {ok:true,json:async()=>data};
}
const context={document:{getElementById:el,createElement:n=>n==='video'?new Video():canvas()},window:{AudioContext},Image,URL:{createObjectURL(){return'blob:test'},revokeObjectURL(){}},Blob,AbortController,DOMException,fetch:fetchFake,setTimeout,clearTimeout,setInterval,clearInterval,console,queueMicrotask};
vm.createContext(context);vm.runInContext(source,context);
class File extends Blob{constructor(data,name){super(data);this.name=name}}
(async()=>{
 el('mediaPhoto').files=[new File(['png'],'test.png')];el('mediaPhoto').onchange();await el('mediaPhotoSend').onclick();assert.equal(saved.at(-1).name,'test.bin');let b=fs.readFileSync(path.join(out,'test.bin'));assert.equal(b.length,480*480*2+4);assert.equal(b.readUInt32LE(),4|(480<<10)|(480<<21));assert.equal(b.readUInt16LE(4),0xf800);
 el('mediaVideo').files=[new File(['mp4'],'clip.mp4')];el('mediaVideo').onchange();await el('mediaVideoSend').onclick();assert.equal(saved.at(-1).name,'clip.rvf');assert(saved.some(s=>s.name==='clip.wav'));
 b=fs.readFileSync(path.join(out,'clip.rvf'));assert.equal(b.subarray(0,4).toString(),'RVF1');assert.equal(b.readUInt16LE(6),32);assert.equal(b.readUInt16LE(8),320);assert.equal(b.readUInt16LE(10),240);assert.equal(b.readUInt32LE(24),1);assert.equal(b.length,32+12*153600);
 b=fs.readFileSync(path.join(out,'clip.wav'));assert.equal(b.length,44+52920*4);assert.equal(b.readUInt32LE(24),44100);assert.equal(b.readUInt16LE(22),2);
 el('mediaFirmware').files=[new File([fs.readFileSync(path.join(root,'build/esp32_smart_voice_box.bin'))],'firmware.bin')];await el('mediaFirmwareSend').onclick();assert.equal(saved.at(-1).kind,'firmware');
 cancelAt=65536;el('mediaPhotoName').value='cancelled';await el('mediaPhotoSend').onclick();assert.equal(sessions.size,0);assert(cancelled>0);assert(!el('mediaPhotoSend').disabled&&el('mediaCancel').hidden);
 fs.writeFileSync(path.join(out,'summary.json'),JSON.stringify(saved,null,2));console.log('Production media JS passed: photo RGB565, bounded RVF chunks, resampled stereo WAV, firmware selection and cancellation; codec/DOM operations are fakes');
})().catch(e=>{console.error(e);process.exitCode=1});
