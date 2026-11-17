#pragma once
static const char MUSIC_WEB_PAGE[] = R"HTML(<!doctype html>
<html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>随身音乐 · ESP32</title>
<style>
:root{font:16px system-ui,sans-serif;color:#233b32;background:#edf4eb;color-scheme:light}*{box-sizing:border-box}
body{max-width:600px;margin:auto;padding:28px 18px 40px}header{display:flex;align-items:center;justify-content:space-between}
.eyebrow{font-size:12px;letter-spacing:3px;color:#66806d}h1{font-size:32px;margin:8px 0}h2{font-size:18px;margin:0 0 14px}
.badge{font-size:12px;background:#dde9d8;padding:7px 11px;border-radius:30px}section{background:#fffef9;border:1px solid #dbe6d7;border-radius:22px;padding:22px;margin-top:18px}
p{line-height:1.6;color:#64766a;font-size:14px}button,input{font:inherit}button{cursor:pointer;border:0;border-radius:13px;padding:13px 16px;background:#477c50;color:#fff;min-height:46px}button:disabled{opacity:.5;cursor:default}
.secondary{background:#e8eee3;color:#294c33}.row{display:flex;gap:10px;align-items:center}.row input{flex:1;min-width:0}input[type=text]{width:100%;border:1px solid #ccd9c8;border-radius:13px;padding:13px;background:#fff}
input[type=file]{max-width:100%;font-size:14px;margin:6px 0 18px}input[type=range],progress{width:100%;accent-color:#477c50}progress{height:8px;margin-top:14px}#now{font-size:21px;font-weight:600;line-height:1.45;overflow-wrap:anywhere}#reply{white-space:pre-wrap;line-height:1.7;overflow-wrap:anywhere;color:#526657;font-size:14px}
.controls{display:grid;grid-template-columns:repeat(3,1fr);gap:9px;margin:20px 0}.small{font-size:12px;color:#768779}#notice{min-height:22px;margin:10px 0 0;font-size:14px;color:#477c50}.error{color:#ad493e!important}.song{display:flex;gap:12px;align-items:center;padding:14px 0;border-top:1px solid #e7eddf}.song:first-child{margin-top:14px}.songInfo{flex:1;min-width:0}.songName{font-weight:600;overflow-wrap:anywhere}.songArtist{overflow-wrap:anywhere;font-size:13px;color:#768779;margin-top:5px}.song button{flex-shrink:0;font-size:14px;padding:11px}#savedPath{overflow-wrap:anywhere}.saveOption{display:flex;align-items:flex-start;gap:8px;font-size:14px;line-height:1.5;margin:14px 0}.saveOption input{width:18px;height:18px;accent-color:#477c50;flex-shrink:0;margin-top:2px}footer{padding-top:22px;text-align:center;font-size:12px;color:#7e8d7b}
</style>
<header><div><div class="eyebrow">ESP32 MUSIC</div><h1>随身音乐</h1></div><span class="badge" id="connection">连接中</span></header>
<p>手机选歌，设备播放。连接此设备热点即可使用本地点播。</p>
<section><h2>正在播放</h2><div class="row"><div id="now">等待选择歌曲</div><button id="download" class="secondary" type="button" disabled>下载</button></div><div class="controls"><button class="secondary" data-command="暂停">暂停</button><button data-command="继续播放">继续</button><button class="secondary" id="stop">停止</button></div>
<div class="row"><label for="volume">设备音量</label><output id="volumeText">—</output></div><input id="volume" type="range" min="0" max="100" value="50"></section>
<section><h2>手机歌曲</h2><p>选择本机 MP3，上传完成后由设备扬声器播放。单个文件最多 20 MiB，需要插入 SD 卡。</p>
<input id="file" type="file" accept="audio/mpeg,.mp3"><div class="row"><button id="upload">上传并播放</button><button id="cancel" class="secondary" hidden>取消上传</button></div><progress id="progress" value="0" max="100" hidden></progress><div id="notice" role="status"></div></section>
<section><h2>网易云点歌</h2><p>设备先连上可上网的 WiFi 并插入 SD 卡，搜索后直接选歌播放。播放只使用可清理的临时缓存。点正在播放旁的“下载”，才会把当前这首歌转换成 WAV 和 LRC 并放入歌单。</p>
<form id="search" class="row"><input id="query" type="text" maxlength="80" placeholder="输入歌名或歌手" aria-label="搜索歌名或歌手"><button>搜索</button></form>
<form id="commandForm" class="row" style="margin-top:10px"><input id="command" type="text" maxlength="80" placeholder="例如：播放第2首" aria-label="音乐指令"><button class="secondary">发送</button></form><div id="results"></div><p id="reply" role="status"></p><button id="playSaved" class="secondary" hidden>播放已保存的 WAV</button><p id="savedPath" class="small"></p></section>
<footer><a href="/settings" style="color:#477c50">服务配置 · AI / 天气 API</a><p>本地连接 · 无需安装 App</p></footer>
<script>
const $=id=>document.getElementById(id);let uploadRequest=null,volumeBusy=false,lastSongs='',savedIndex=-1;
function notice(text,error=false){$('notice').textContent=text;$('notice').classList.toggle('error',error)}
async function api(path,body){const response=await fetch(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});const data=await response.json();if(!response.ok||!data.ok)throw Error(data.error||'操作失败');return data}
async function command(text){try{await api('/api/music',{command:text,save:false});await refresh()}catch(error){notice(error.message,true)}}
async function refresh(){if(uploadRequest){$('connection').textContent='上传中';return}try{const response=await fetch('/api/status',{cache:'no-store'});if(!response.ok)throw Error();const state=await response.json();$('connection').textContent=state.preparing?'准备 / 保存中':state.playing?'播放中':'已连接';$('now').textContent=state.title||'等待选择歌曲';$('download').disabled=!!state.preparing||!state.can_download;$('reply').textContent=state.reply||'';renderSongs(state.songs||[]);savedIndex=Number.isInteger(state.saved_index)?state.saved_index:-1;$('playSaved').hidden=savedIndex<0;$('savedPath').textContent=state.saved_path||'';if(!volumeBusy){$('volume').value=state.volume;$('volumeText').textContent=state.volume+'%'}}catch(error){$('connection').textContent='连接中断'}}
function renderSongs(songs){const key=JSON.stringify(songs);if(key===lastSongs)return;lastSongs=key;const list=$('results');list.replaceChildren();songs.forEach(song=>{const row=document.createElement('div');row.className='song';const info=document.createElement('div');info.className='songInfo';const title=document.createElement('div');title.className='songName';title.textContent=song.index+'. '+song.title;const artist=document.createElement('div');artist.className='songArtist';artist.textContent=song.artist||'未知歌手';info.append(title,artist);const button=document.createElement('button');button.type='button';button.textContent='播放';button.onclick=()=>command('播放第'+song.index+'首');row.append(info,button);list.append(row)})}
document.querySelectorAll('[data-command]').forEach(button=>button.onclick=()=>command(button.dataset.command));
$('download').onclick=async()=>{try{await api('/api/music',{download:true});await refresh()}catch(error){notice(error.message,true)}};
$('playSaved').onclick=async()=>{if(savedIndex<0)return;try{await api('/api/music',{local_track:savedIndex});await refresh()}catch(error){notice(error.message,true)}};
$('search').onsubmit=event=>{event.preventDefault();const text=$('query').value.trim();if(text)command('搜歌 '+text)};
$('commandForm').onsubmit=event=>{event.preventDefault();const text=$('command').value.trim();if(text)command(text)};
$('volume').oninput=()=>{volumeBusy=true;$('volumeText').textContent=$('volume').value+'%'};
$('volume').onchange=async()=>{try{await api('/api/volume',{volume:Number($('volume').value)})}catch(error){notice(error.message,true)}finally{volumeBusy=false}};
function uploadDone(){uploadRequest=null;$('upload').disabled=false;$('cancel').hidden=true}
$('cancel').onclick=()=>{if(uploadRequest)uploadRequest.abort()};
$('stop').onclick=()=>{if(uploadRequest)uploadRequest.abort();command('停止')};
$('upload').onclick=()=>{const file=$('file').files[0];if(!file){notice('请先选择 MP3 文件',true);return}if(!/\.mp3$/i.test(file.name)||file.size<128||file.size>20*1024*1024){notice('请选择 20 MiB 以内的 MP3 文件',true);return}
const request=new XMLHttpRequest();uploadRequest=request;$('upload').disabled=true;$('cancel').hidden=false;$('progress').hidden=false;$('progress').value=0;notice('正在上传，请保持此页面打开…');request.open('POST','/api/upload');request.timeout=180000;request.setRequestHeader('Content-Type','application/octet-stream');request.setRequestHeader('X-Filename',encodeURIComponent(file.name));
request.upload.onprogress=event=>{if(event.lengthComputable)$('progress').value=Math.round(event.loaded/event.total*100)};
request.onload=()=>{try{const data=JSON.parse(request.responseText);if(request.status!==200||!data.ok)throw Error(data.error||'上传失败');notice('上传完成，已提交设备播放');refresh()}catch(error){notice(error.message,true)}finally{uploadDone()}};
request.onerror=()=>{notice('连接中断，请重连设备热点',true);uploadDone()};request.ontimeout=()=>{notice('上传超时，请重试',true);uploadDone()};request.onabort=()=>{notice('已取消上传');uploadDone()};request.send(file)};
refresh();setInterval(refresh,1500);
</script></html>)HTML";
