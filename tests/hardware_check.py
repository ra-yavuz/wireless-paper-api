#!/usr/bin/env python3
"""Live smoke test. Changes the screen; optional receive-only radio check."""
import argparse
import json
import time
from pathlib import Path
import requests

p=argparse.ArgumentParser(description=__doc__, epilog="Provided as is, without warranty. You accept all risk from installation and use.")
p.add_argument('--url',required=True)
p.add_argument('--radio-mhz',type=float,help='Temporarily initialize LoRa receive at this frequency, then restore its setting')
args=p.parse_args()
s=requests.Session();s.trust_env=False;base=args.url.rstrip('/')
def get(path):
    r=s.get(base+path,timeout=10);r.raise_for_status();return r
def wait(job):
    for _ in range(120):
        d=get('/api/status').json()
        if not d['busy']:
            assert not d['display_error'],d
            assert d['completed_job']==job,d
            return
        time.sleep(.5)
    raise AssertionError('Display update timeout')
initial=get('/api/status').json()
assert initial['frame_bytes']==3904 and initial['http_port']==80
assert initial.get('hotspot_mode') in ('first-hour','always') and initial.get('wifi_shutdown_allowed') is False,'Refusing shutdown-regression tests on firmware without station connectivity protection'
def check_connectivity_policy():
    for mode in ['radio','off','sleep','deep-sleep']:
        r=s.post(base+'/api/power',json={'mode':mode},timeout=10)
        assert r.status_code==403,(mode,r.status_code,r.text)
    for action in [{'action':'wifi','seconds':15},{'action':'wifi','enabled':False},{'action':'off'}]:
        r=s.post(base+'/api/display',json=action,timeout=10)
        assert r.status_code==400,(action,r.status_code,r.text)
    r=s.post(base+'/api/power',json={'mode':'online'},timeout=10);r.raise_for_status()
    d=get('/api/status').json()
    assert d['hotspot_active'] and not d['wifi_shutdown_allowed']
    assert d['hotspot_mode'] in ('first-hour','always')
    assert d['power_mode']=='online' and d['ip']==initial['ip']
check_connectivity_policy()
wait(initial['accepted_job'] if initial['busy'] else initial['completed_job'])
before=get('/api/frame').content
for doc in [[],{'ops':'bad'},{'ops':[{'type':'line','x':1,'y':2}]},{'text':'bad','size':99},{'clear':'bad'},
            {'text':'bad','color':'red'},{'text':'bad','background':'blue'},
            {'text':'bad','x':'8'},{'text':'bad','wrap':'yes'},
            {'ops':[{'type':'text','x':0,'y':0,'text':'bad','color':'red'}]}]:
    r=s.post(base+'/api/display',json=doc,timeout=10)
    assert r.status_code==400,(doc,r.status_code,r.text)
r=s.post(base+'/api/frame',data=b'\0invalid',headers={'Content-Type':'application/octet-stream'},timeout=10)
assert r.status_code==400
assert get('/api/frame').content==before
frame=bytes((0 if i%7==0 else 255 if i%7==1 else (i*17)&255) for i in range(3904))
r=s.post(base+'/api/frame',data=frame,headers={'Content-Type':'application/octet-stream'},timeout=10)
assert r.status_code==202,(r.status_code,r.text);job=r.json()['job']
r=s.post(base+'/api/display',json={'text':'Must be rejected while updating'},timeout=10)
assert r.status_code==409,(r.status_code,r.text)
wait(job)
assert get('/api/frame').content==frame,'Binary image did not round trip exactly'
def draw(doc):
    r=s.post(base+'/api/display',json=doc,timeout=10)
    assert r.status_code==202,(doc,r.status_code,r.text)
    wait(r.json()['job']);return get('/api/frame').content
small=draw({'text':'A','size':1,'x':0,'y':0,'color':'black','background':'white','wrap':False})
large=draw({'text':'A','size':3,'x':12,'y':15,'color':'white','background':'black','wrap':False})
expected=bytearray(3904)
for y in range(122):
    for x in range(250):expected[y*32+x//8]|=128>>(x%8)
for y in range(8):
    for x in range(6):
        if small[y*32+x//8]&(128>>(x%8)):
            for dy in range(3):
                for dx in range(3):
                    xx,yy=12+x*3+dx,15+y*3+dy
                    expected[yy*32+xx//8]&=~(128>>(xx%8))
assert large==bytes(expected),'Text size, position, ink, or background pixels are wrong'
inherited=draw({'size':3,'color':'#FFFFFF','background':'#000000','wrap':False,
                'ops':[{'type':'text','x':12,'y':15,'text':'A'}]})
assert inherited==large,'Text operation did not inherit settings'
layered=draw({'clear':False,'ops':[{'type':'rect','x':210,'y':100,'w':10,'h':10,'fill':True,'color':'white'}]})
for y in range(100,110):
    for x in range(210,220):expected[y*32+x//8]&=~(128>>(x%8))
assert layered==bytes(expected),'Named shape color or retained background is wrong'
assert 'Text and drawing' in get('/api').text
if args.radio_mhz:
    try:
        r=s.post(base+'/api/radio',json={'frequency_mhz':args.radio_mhz},timeout=20)
        assert r.status_code==200,(r.status_code,r.text)
        assert r.json()['radio_ready'],r.text
        check_connectivity_policy()
        # TX-disabled requests must never emit a radio packet.
        r=s.post(base+'/api/lora/send',json={'text':'not transmitted'},timeout=10)
        assert r.status_code==403,(r.status_code,r.text)
        r=s.post(base+'/api/radio',json={'bandwidth_khz':250,'spreading_factor':8,'coding_rate':6,'sync_word':19,'power_dbm':2,'preamble':12,'crc':False,'tx_enabled':False,'duty_cycle_percent':2},timeout=20)
        r.raise_for_status();settings=r.json()['radio']
        assert settings['bandwidth_khz']==250 and settings['spreading_factor']==8 and settings['crc'] is False
        r=s.post(base+'/api/radio',json={'bandwidth_khz':17},timeout=10);assert r.status_code==400
        r=s.post(base+'/api/radio',json={'tx_enabled':True},timeout=20);r.raise_for_status()
        for payload in [{},{'text':''},{'hex':'0'},{'hex':'zz'},{'text':'x'*256},{'text':'x','hex':'01'}]:
            r=s.post(base+'/api/lora/send',json=payload,timeout=10)
            assert r.status_code==400,(payload,r.status_code,r.text)
        assert get('/api/status').json()['lora_sent_packets']==initial['lora_sent_packets']
        assert isinstance(get('/api/lora/messages?after=0').json()['messages'],list)
        r=s.get(base+'/api/lora/messages?after=bad',timeout=10);assert r.status_code==400
    finally:
        r=s.post(base+'/api/radio',json=initial['radio'],timeout=20)
        r.raise_for_status()
message='Paper ready\n'+initial['ip']+'\n\nHotspot:\n192.168.4.1'
r=s.post(base+'/api/display',json={'text':message,'size':2},timeout=10)
assert r.status_code==202,(r.status_code,r.text);wait(r.json()['job'])
assert any(get('/api/frame').content),'Text produced a blank frame'
final=get('/api/status').json()
print(json.dumps({'passed':['HTTP status','first-hour/always hotspot policy','legacy shutdown commands rejected','malformed input rejection','no screen change for rejected requests','binary bitmap including null bytes','busy rejection','completed image round trip','text size and positioning','named ink and background colors','global text settings inherited by operations','retained image with named shape colors','API reference','text pixels']+(['SX1262 receive initialization','radio configuration round trip','TX disabled and malformed payloads rejected without transmission','packet log endpoint'] if args.radio_mhz else []),'final_status':final},indent=2))
