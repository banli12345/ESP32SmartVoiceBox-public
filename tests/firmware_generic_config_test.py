"""Check initial WiFi defaults in the actual linked ESP32 ELF, including BSS."""
from pathlib import Path
import json,struct,sys
from firmware_psram_budget_test import symbols

def inspect(path):
 data=path.read_bytes();h=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
 sections=[struct.unpack_from('<IIIIIIIIII',data,h[6]+i*h[11])for i in range(h[12])]
 values=symbols(path);result={}
 for label,n in [('wifi_ssid_storage',33),('wifi_password_storage',64)]:
  matches=[(name,addr)for name,addr in values.items()if name.endswith(label)]
  assert len(matches)==1,(label,'symbol unavailable')
  name,addr=matches[0];found=False
  for s in sections:
   if s[2]&2 and s[3]<=addr and addr+n<=s[3]+s[5]:
    initial=bytes(n)if s[1]==8 else data[s[4]+addr-s[3]:s[4]+addr-s[3]+n]
    result[label]={'bytes':n,'initially_empty':len(initial)==n and not any(initial),'section_type':s[1]}
    assert result[label]['initially_empty'],label+' contains compiled provisioning data'
    found=True;break
  assert found,label+' not found in loadable sections'
 return {'wifi_defaults':result,'passed':True}
if __name__=='__main__':print(json.dumps(inspect(Path(sys.argv[1]))))
