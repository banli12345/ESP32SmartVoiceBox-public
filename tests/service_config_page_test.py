"""Execute the actual configuration page script with DOM/fetch boundaries."""
from pathlib import Path
import json,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
NODE=ROOT/'.tools/node-v24.21.0-win-x64/node.exe'
JS=r'''
const vm=require('node:vm'),assert=require('node:assert/strict');
const html=@HTML@;const nodes={};
function element(id){return nodes[id]??={value:'',checked:false,hidden:false,disabled:false,textContent:'',placeholder:'',classList:{toggle(){}}};}
for(const tag of html.matchAll(/<input\b[^>]+>/g)){
 const id=/\bid="([^"]+)"/.exec(tag[0]);if(!id)continue;
 const n=element(id[1]);n.value=/\bvalue="([^"]*)"/.exec(tag[0])?.[1]??'';n.checked=/\bchecked\b/.test(tag[0]);
 if(/type="password"/.test(tag[0]))assert.equal(n.value,'');
}
const buttons=[element('saveAI'),element('saveWeather'),element('reboot')],calls=[];
let failSave=false;
let state={ok:true,token:'a'.repeat(32),ai:{configured:true,has_key:true,pending:false,api_base:'https://api.deepseek.com',model:'deepseek-flash',speak:true},weather:{configured:true,has_key:true,life_has_key:true,pending:false,now_url:'http://api.seniverse.com/v3/weather/now.json',daily_url:'http://api.seniverse.com/v3/weather/daily.json',life_url:'https://api.seniverse.com/v3/life/suggestion.json',life_use_weather_key:true}};
const ctx=vm.createContext({document:{getElementById:element,querySelectorAll:()=>buttons},
 fetch:async(path,options)=>{
  calls.push({path,options});assert.equal(options.cache,'no-store');
  if(path==='/api/config/status')return{ok:true,json:async()=>structuredClone(state)};
  assert.equal(options.headers['X-Config-Token'],state.token);assert.equal(options.method,'POST');
  if(failSave)return{ok:false,json:async()=>({ok:false,error:'保存失败'})};
  const body=JSON.parse(options.body);
  if(path==='/api/config/ai'){Object.assign(state.ai,{api_base:body.api_base,model:body.model,speak:body.speak,pending:true});}
  return{ok:true,json:async()=>({ok:true})};
 },console:{log(){throw Error('Page must not log configuration')}}});
const tick=()=>new Promise(r=>setImmediate(r));
(async()=>{
 vm.runInContext(/<script>([\s\S]*?)<\/script>/.exec(html)[1],ctx);await tick();
 assert.equal(calls.length,1);assert.equal(calls[0].path,'/api/config/status');assert.equal(element('aiKey').value,'');
 assert.equal(element('aiState').textContent,'已配置');assert.equal(buttons[0].disabled,false);
 element('weatherKey').value='unsaved-weather-key';element('aiKey').value='new-ai-key';element('aiModel').value='new-model';
 await element('aiForm').onsubmit({preventDefault(){}});
 let sent=calls.find(c=>c.path==='/api/config/ai');assert.equal(JSON.parse(sent.options.body).api_key,'new-ai-key');
 assert.equal(element('aiKey').value,'');assert.equal(element('weatherKey').value,'unsaved-weather-key');
 assert.equal(element('aiState').textContent,'已保存 · 待重启');
 element('aiKey').value='another-unsaved-ai-key';element('lifeUseWeather').checked=false;element('lifeUseWeather').onchange();assert.equal(element('lifeKeyRow').hidden,false);
 element('lifeKey').value='new-life-key';await element('weatherForm').onsubmit({preventDefault(){}});
 sent=calls.find(c=>c.path==='/api/config/weather');assert.equal(JSON.parse(sent.options.body).life_api_key,'new-life-key');
 assert.equal(element('weatherKey').value,'');assert.equal(element('lifeKey').value,'');assert.equal(element('aiKey').value,'another-unsaved-ai-key');
 failSave=true;await element('aiForm').onsubmit({preventDefault(){}});assert.equal(element('aiKey').value,'another-unsaved-ai-key');assert.equal(element('aiStatus').textContent,'保存失败');
 failSave=false;await element('reboot').onclick();assert.equal(buttons[0].disabled,true);assert(element('globalStatus').textContent.includes('重启'));
 assert.equal(calls.filter(c=>c.path==='/api/config/reboot').length,1);
 console.log('Service page PASS: no auto-save/reboot, hidden saved keys, independent forms, blank preservation, CSRF header, failure retention and explicit restart');
})().catch(e=>{console.error(e);process.exitCode=1});
'''
def main():
 text=(ROOT/'src/service_config_page.h').read_text(encoding='utf8')
 html=text.split('R"HTML(',1)[1].split(')HTML"',1)[0]
 assert 'localStorage' not in html and 'sessionStorage' not in html and 'innerHTML' not in html
 assert not re.search(r'<script[^>]+src=',html)
 for field,limit in [('aiKey',255),('weatherKey',95),('lifeKey',95)]:
  tag=re.search(r'<input[^>]+id="'+field+r'"[^>]*>',html).group()
  assert 'type="password"'in tag and f'maxlength="{limit}"'in tag
 with tempfile.TemporaryDirectory(prefix='service-page-')as tmp:
  p=Path(tmp)/'test.cjs';p.write_text(JS.replace('@HTML@',json.dumps(html,ensure_ascii=False)),encoding='utf8')
  subprocess.run([str(NODE),str(p)],check=True,timeout=30)
if __name__=='__main__':main()
