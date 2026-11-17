"""Test runtime weather/life and AI SD imports; no real credentials/devices."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SCRIPT=ROOT/'scripts/configure_services.py'
WK='weather-unit-key'; AK='ai-unit-key-value'
def services():
 return dict(version=1,weather=dict(api_key=WK),ai=dict(version=1,api_base='https://api.deepseek.com',api_key=AK,model='deepseek-flash',speak=True))
class ConfigTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
  self.dir=Path(self.tmp.name);self.source=self.dir/'services.private.json'
  self.weather=self.dir/'sd/config/weather.json';self.ai=self.dir/'sd/config/ai.json'
 def write(self,d):self.source.write_text(json.dumps(d),encoding='utf8')
 def cli(self,w=None,a=None):
  return subprocess.run([sys.executable,str(SCRIPT),str(self.source),'--weather-output',str(w or self.weather),'--ai-output',str(a or self.ai)],capture_output=True,text=True,encoding='utf8')
 def test_two_imports_not_header_no_secrets(self):
  self.write(services());header=self.dir/'private_config.h';header.write_text('#define DEVICE_WIFI_SSID "HomeNet"\n')
  r=self.cli();self.assertEqual(r.returncode,0,r.stderr)
  self.assertEqual(header.read_text(),'#define DEVICE_WIFI_SSID "HomeNet"\n')
  self.assertNotIn(WK,r.stdout+r.stderr);self.assertNotIn(AK,r.stdout+r.stderr)
  w=json.loads(self.weather.read_text());self.assertEqual(w['api_key'],WK);self.assertEqual(w['life_api_key'],WK)
  for field,path in [('now_url','weather/now'),('daily_url','weather/daily'),('life_url','life/suggestion')]:self.assertIn('/v3/'+path+'.json',w[field])
  self.assertEqual(json.loads(self.ai.read_text()),services()['ai'])
  for p in [self.weather,self.ai]:self.assertFalse(p.read_bytes().startswith(b'\xef\xbb\xbf'));self.assertLessEqual(p.stat().st_size,2048)
  self.assertIn('No rebuild',r.stdout)
 def test_custom_endpoints_separate_life_key(self):
  d=services();d['weather'].update(now_url='https://weather.example.test:8443/now',daily_url='http://192.0.2.1:8080/daily',life_url='https://life.example.test/index',life_api_key='separate-life-key')
  self.write(d);r=self.cli();self.assertEqual(r.returncode,0,r.stderr)
  w=json.loads(self.weather.read_text())
  for k,v in d['weather'].items():self.assertEqual(w[k],v)
 def test_bom(self):
  self.source.write_text(json.dumps(services()),encoding='utf-8-sig');self.assertEqual(self.cli().returncode,0)
 def test_invalid_fields_preserve_outputs(self):
  self.weather.parent.mkdir(parents=True);self.weather.write_text('old-weather');self.ai.write_text('old-ai')
  cases=[('weather','api_key',''),('weather','api_key','has space'),('weather','api_key','w'*96),('weather','api_key','YOUR_SECRET'),('weather','life_api_key',''),('weather','life_api_key','x\0y'),('weather','now_url','https://user:pass@host/x'),('weather','now_url','http://'),('weather','now_url','https://host:0/x'),('weather','now_url','https://host:65536/x'),('weather','now_url','https://host:abc/x'),('weather','now_url','https://host:/x'),('weather','now_url','ftp://host/x'),('weather','daily_url','https://host/x?key=secret'),('weather','life_url','https://host/x#fragment'),('weather','life_url','https://host/\nsecret'),('weather','life_url','https://host/'+'a'*192),('ai','api_base','http://api.deepseek.com'),('ai','api_key','short'),('ai','api_key','x'*256),('ai','model','bad model'),('ai','speak','true')]
  for group,key,value in cases:
   with self.subTest(group=group,key=key):
    d=services();d[group][key]=value;self.write(d);r=self.cli();self.assertNotEqual(r.returncode,0)
    self.assertEqual(self.weather.read_text(),'old-weather');self.assertEqual(self.ai.read_text(),'old-ai')
    for secret in [WK,AK,'user:pass']:self.assertNotIn(secret,r.stdout+r.stderr)
 def test_invalid_json_version_duplicates(self):
  for text in ['{"api_key":"sensitive','{"version":1,"version":1}',json.dumps(dict(services(),version=True)),'[]']:
   self.source.write_text(text,encoding='utf8');r=self.cli();self.assertNotEqual(r.returncode,0)
   self.assertFalse(self.weather.exists());self.assertFalse(self.ai.exists());self.assertNotIn('sensitive',r.stdout+r.stderr)
 def test_paths_must_differ(self):
  self.write(services());before=self.source.read_bytes()
  for w,a in [(self.source,self.ai),(self.weather,self.weather)]:self.assertNotEqual(self.cli(w,a).returncode,0);self.assertEqual(self.source.read_bytes(),before)
 def test_boundary_keys_and_missing_ai(self):
  d=services();d['weather']['api_key']='w'*95;d['ai']['api_key']='k'*255;self.write(d)
  r=self.cli();self.assertEqual(r.returncode,0,r.stderr)
  self.assertEqual(len(json.loads(self.weather.read_text())['api_key']),95)
  self.assertEqual(len(json.loads(self.ai.read_text())['api_key']),255)
  d.pop('ai');self.write(d);self.assertNotEqual(self.cli().returncode,0)
if __name__=='__main__':unittest.main()