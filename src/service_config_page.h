#pragma once
static const char SERVICE_CONFIG_PAGE[] = R"HTML(<!doctype html>
<html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>服务配置 · Breeze OS</title>
<style>
:root{font:16px system-ui,sans-serif;color:#233b32;background:#edf4eb;color-scheme:light}*{box-sizing:border-box}body{max-width:640px;margin:auto;padding:24px 18px 40px}a{color:#477c50;text-decoration:none}header{display:flex;align-items:center;justify-content:space-between;gap:12px}small,.eyebrow{color:#738775;font-size:12px}.eyebrow{letter-spacing:3px}h1{font-size:30px;margin:8px 0}h2{font-size:20px;margin:0}p{color:#64766a;font-size:14px;line-height:1.7}section{background:#fffef9;border:1px solid #dbe6d7;border-radius:22px;padding:22px;margin-top:18px}.cardhead{display:flex;align-items:center;justify-content:space-between;gap:10px}.badge{font-size:12px;padding:6px 10px;border-radius:20px;background:#e6eddf;color:#477c50;white-space:nowrap}label.field{display:block;margin:17px 0 6px;font-size:14px;font-weight:600}input[type=text],input[type=password]{display:block;width:100%;min-height:46px;border:1px solid #ccd9c8;border-radius:12px;padding:12px;font:inherit;background:#fff;color:#233b32}input:focus{outline:2px solid #a9c998;outline-offset:1px}input[type=checkbox]{width:18px;height:18px;accent-color:#477c50}.check{display:flex;align-items:center;gap:8px;margin:16px 0;font-size:14px;font-weight:400}button{border:0;border-radius:13px;min-height:46px;padding:12px 16px;font:inherit;background:#477c50;color:#fff;cursor:pointer}.wide{width:100%;margin-top:18px}.secondary{background:#e6eddf;color:#385e40}button:disabled{opacity:.5;cursor:default}.hint{display:block;font-size:12px;color:#7b8c7d;margin-top:7px}.status{font-size:14px;line-height:1.6;min-height:22px;margin-top:12px;color:#477c50;overflow-wrap:anywhere}.error{color:#ad493e}.actions{display:flex;gap:10px;align-items:center}.actions button{flex:1}footer{text-align:center;color:#7b8c7d;font-size:12px;line-height:1.8;padding:24px 4px}[hidden]{display:none!important}
</style>
<header><div><div class="eyebrow">BREEZE OS</div><h1>服务配置</h1></div><a href="/">返回随身音乐</a></header>
<p>填入自己的接口和密钥，分别保存后重启生效。已有密钥不会显示，留空即可保留。</p>
<section><form id="aiForm" autocomplete="off">
<div class="cardhead"><h2>AI 助手</h2><span id="aiState" class="badge">读取中</span></div>
<label class="field" for="aiBase">API 基础地址</label><input id="aiBase" type="text" inputmode="url" maxlength="191" value="https://api.deepseek.com" spellcheck="false" required>
<label class="field" for="aiKey">API Key</label><input id="aiKey" type="password" maxlength="255" autocomplete="off" spellcheck="false" placeholder="输入自己的密钥">
<span id="aiKeyHint" class="hint">密钥仅写入设备，不会回显。</span>
<label class="field" for="aiModel">模型名称</label><input id="aiModel" type="text" maxlength="63" value="deepseek-flash" spellcheck="false" required>
<label class="check"><input id="aiSpeak" type="checkbox" checked>回答后朗读</label>
<p>地址需要兼容当前 DeepSeek 文本与语音协议。</p>
<button class="wide" data-write type="submit" disabled>保存 AI 配置</button><div id="aiStatus" class="status" role="status" aria-live="polite"></div>
</form></section>
<section><form id="weatherForm" autocomplete="off">
<div class="cardhead"><h2>天气与生活指数</h2><span id="weatherState" class="badge">读取中</span></div>
<label class="field" for="weatherKey">天气 API Key</label><input id="weatherKey" type="password" maxlength="95" autocomplete="off" spellcheck="false" placeholder="输入自己的密钥"><span id="weatherKeyHint" class="hint">实况和预报使用此密钥。</span>
<label class="field" for="nowUrl">天气实况地址</label><input id="nowUrl" type="text" inputmode="url" maxlength="191" value="http://api.seniverse.com/v3/weather/now.json" spellcheck="false" required>
<label class="field" for="dailyUrl">三日预报地址</label><input id="dailyUrl" type="text" inputmode="url" maxlength="191" value="http://api.seniverse.com/v3/weather/daily.json" spellcheck="false" required>
<label class="field" for="lifeUrl">生活指数地址</label><input id="lifeUrl" type="text" inputmode="url" maxlength="191" value="https://api.seniverse.com/v3/life/suggestion.json" spellcheck="false" required>
<label class="check"><input id="lifeUseWeather" type="checkbox" checked>生活指数沿用天气密钥</label>
<div id="lifeKeyRow" hidden><label class="field" for="lifeKey">生活指数 API Key</label><input id="lifeKey" type="password" maxlength="95" autocomplete="off" spellcheck="false" placeholder="输入独立的生活指数密钥"><span id="lifeKeyHint" class="hint">已有密钥留空保留。</span></div>
<p>支持兼容心知天气协议的 HTTP / HTTPS 地址，不含“?”查询参数；接口与地区权限取决于自己的账号。</p>
<button class="wide" data-write type="submit" disabled>保存天气配置</button><div id="weatherStatus" class="status" role="status" aria-live="polite"></div>
</form></section>
<section><h2>保存后统一重启</h2><p>两项可以分别保存。重启后设备使用新配置，原有 WiFi、热点和音量设置保留。</p><div class="actions"><button id="reload" type="button" class="secondary">重新读取</button><button id="reboot" type="button" data-write disabled>重启并生效</button></div><div id="globalStatus" class="status" role="status" aria-live="polite"></div></section>
<footer>配置保存在设备 NVS，断电保留。<br>原有 SD 配置导入仍可使用。</footer>
<script>
const $=id=>document.getElementById(id);let token='',working=false,restarting=false;
function message(id,text,error=false){const node=$(id);node.textContent=text;node.classList.toggle('error',error)}
function busy(value){working=value;document.querySelectorAll('[data-write]').forEach(b=>b.disabled=value||!token||restarting);$('reload').disabled=value||restarting}
function lifeToggle(){$('lifeKeyRow').hidden=$('lifeUseWeather').checked}
function keyHint(id,hint,configured){$(id).placeholder=configured?'已保存，留空保留':'输入自己的密钥';$(hint).textContent=configured?'密钥已保存在设备，留空保留，不会回显。':'尚未配置，请输入自己的密钥。'}
async function api(path,body){const options={cache:'no-store',credentials:'same-origin'};if(body!==undefined){options.method='POST';options.headers={'Content-Type':'application/json','X-Config-Token':token};options.body=JSON.stringify(body)}const response=await fetch(path,options);let data;try{data=await response.json()}catch(_){throw Error('设备未返回有效结果，请重试')}if(!response.ok||!data.ok)throw Error(data.error||'请求失败，请重试');return data}
async function load(scope='all'){const state=await api('/api/config/status');token=state.token||'';if(!token)throw Error('配置页面已失效，请刷新');if(scope==='all'||scope==='ai'){const a=state.ai;$('aiBase').value=a.api_base;$('aiModel').value=a.model;$('aiSpeak').checked=a.speak;$('aiState').textContent=a.pending?'已保存 · 待重启':a.configured?'已配置':'未配置';keyHint('aiKey','aiKeyHint',a.has_key)}if(scope==='all'||scope==='weather'){const w=state.weather;$('nowUrl').value=w.now_url;$('dailyUrl').value=w.daily_url;$('lifeUrl').value=w.life_url;$('lifeUseWeather').checked=w.life_use_weather_key;$('weatherState').textContent=w.pending?'已保存 · 待重启':w.configured?'已配置':'未配置';keyHint('weatherKey','weatherKeyHint',w.has_key);keyHint('lifeKey','lifeKeyHint',w.life_has_key);lifeToggle()}}
$('lifeUseWeather').onchange=lifeToggle;
$('aiForm').onsubmit=async event=>{event.preventDefault();if(working||!token)return;busy(true);try{await api('/api/config/ai',{api_base:$('aiBase').value,api_key:$('aiKey').value,model:$('aiModel').value,speak:$('aiSpeak').checked});$('aiKey').value='';message('aiStatus','已保存，重启后生效');await load('ai')}catch(error){message('aiStatus',error.message,true)}finally{busy(false)}};
$('weatherForm').onsubmit=async event=>{event.preventDefault();if(working||!token)return;busy(true);try{await api('/api/config/weather',{now_url:$('nowUrl').value,daily_url:$('dailyUrl').value,life_url:$('lifeUrl').value,api_key:$('weatherKey').value,life_api_key:$('lifeKey').value,life_use_weather_key:$('lifeUseWeather').checked});$('weatherKey').value='';$('lifeKey').value='';message('weatherStatus','已保存，重启后生效');await load('weather')}catch(error){message('weatherStatus',error.message,true)}finally{busy(false)}};
$('reload').onclick=async()=>{if(working)return;busy(true);try{await load();message('globalStatus','已重新读取保存状态，已有密钥不会显示')}catch(error){message('globalStatus',error.message,true)}finally{busy(false)}};
$('reboot').onclick=async()=>{if(working||!token)return;busy(true);try{await api('/api/config/reboot',{});restarting=true;message('globalStatus','设备正在重启，请稍后重新连接热点并刷新页面')}catch(error){message('globalStatus',error.message,true)}finally{busy(false)}};
busy(true);load().catch(error=>message('globalStatus',error.message,true)).finally(()=>busy(false));
</script></html>)HTML";
