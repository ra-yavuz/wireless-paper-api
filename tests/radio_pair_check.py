#!/usr/bin/env python3
"""Exercise two HTTP LoRa adapters on a real air link; restore their settings."""
import argparse
import json
from pathlib import Path
import secrets
import time
import requests


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--a', required=True, help='First board HTTP URL')
    parser.add_argument('--b', required=True, help='Second board HTTP URL')
    parser.add_argument('--frequency', type=float, required=True, help='MHz appropriate for both boards and location')
    parser.add_argument('--output', type=Path, help='Save payload and modem evidence without network addresses')
    args = parser.parse_args()
    session = requests.Session(); session.trust_env = False
    urls = {'a': args.a.rstrip('/'), 'b': args.b.rstrip('/')}
    evidence = {'started': time.strftime('%Y-%m-%dT%H:%M:%S%z'), 'boards': {}, 'exchanges': []}
    saved = {}

    def get(board, path):
        r = session.get(urls[board]+path, timeout=10); r.raise_for_status(); return r.json()

    def post(board, path, body):
        r = session.post(urls[board]+path, json=body, timeout=20); r.raise_for_status(); return r.json()

    def config(board, profile):
        d = post(board, '/api/lora/config', profile)
        assert d['radio_ready'], (board, 'receiver failed', d['radio_error'])

    def exchange(sender, receiver, payload, radio=None, receiver_ready=True):
        before = get(receiver, '/api/lora/messages')['latest_id']
        body = {'hex': payload.hex()}
        if radio is not None: body['radio'] = radio
        while True:
            r = session.post(urls[sender]+'/api/lora/send', json=body, timeout=20)
            if r.status_code != 429: break
            time.sleep(max(1, min(3600, int(r.headers.get('Retry-After', '1')))))
        r.raise_for_status(); sent = r.json()
        end = time.monotonic()+10
        received = None
        while time.monotonic() < end:
            log = get(receiver, '/api/lora/messages?after='+str(before))
            received = next((m for m in log['messages'] if m['direction']=='rx' and m['hex']==payload.hex()), None)
            if received: break
            time.sleep(.2)
        assert received, (sender, receiver, 'No exact radio payload received')
        assert sent['receiver_ready'] == receiver_ready, 'Sender did not restore its receiver state'
        evidence['exchanges'].append({'from': sender, 'to': receiver, 'sent': sent, 'received': received})
        print(f'{sender} -> {receiver}: {len(payload)} bytes matched; RSSI {received["rssi"]}, SNR {received["snr"]}', flush=True)
        return sent

    try:
        for board in urls:
            status = get(board, '/api/status'); saved[board] = status['radio']
            evidence['boards'][board] = {k: status[k] for k in ['firmware','board_model','width','height']}
        profile = dict(enabled=True, frequency_mhz=args.frequency, bandwidth_khz=125, spreading_factor=7,
                       coding_rate=5, sync_word=18, power_dbm=-9, preamble=8, crc=True, tx_enabled=True, duty_cycle_percent=1)
        evidence['baseline'] = profile
        for board in urls: config(board, profile)
        nonce = secrets.token_hex(6)
        exchange('a','b', ('Hello from A '+nonce).encode())
        exchange('b','a', ('Reply from B '+nonce).encode())
        exchange('a','b', bytes.fromhex('0001ff00807f000a'))
        r = session.post(urls['a']+'/api/lora/send', json={'text':'paced'}, timeout=10)
        assert r.status_code == 429 and int(r.headers['Retry-After']) > 0
        evidence['pacing_rejected'] = True
        exchange('b','a', bytes(range(255)))
        # The sender has to return to its saved receiver after a request-local SF/BW change.
        alternate = dict(profile, bandwidth_khz=250, spreading_factor=8)
        config('b', alternate)
        sent = exchange('a','b', ('Temporary profile '+nonce).encode(), {'bandwidth_khz':250, 'spreading_factor':8})
        assert sent['temporary_radio'] and sent['radio']['bandwidth_khz']==250
        assert get('a','/api/lora/config')['settings'] == profile
        config('b', profile)
        exchange('b','a', ('After restore '+nonce).encode())
        for invalid in [None, {'spreading_factor':13}, {'frequency_mhz':820}, {'unexpected':True}]:
            r = session.post(urls['a']+'/api/lora/send', json={'text':'invalid','radio':invalid}, timeout=10)
            assert r.status_code == 400
            assert get('a','/api/lora/config')['settings'] == profile
        evidence['invalid_profiles_rejected'] = True
        post('a','/api/lora/activate', {'enabled':False})
        r = session.post(urls['a']+'/api/lora/send', json={'text':'inactive'}, timeout=10)
        assert r.status_code == 409
        sent = exchange('a','b', ('One-shot while inactive '+nonce).encode(), profile, receiver_ready=False)
        # A deliberately inactive baseline is restored, so receiver_ready is false here.
        assert not get('a','/api/lora/status')['ready']
        evidence['completed'] = True
    finally:
        for board, profile in saved.items():
            post(board, '/api/lora/config', profile)
        if args.output:
            args.output.write_text(json.dumps(evidence, indent=2)+'\n')
    print('All real-radio pair checks passed; original radio settings restored.')


if __name__ == '__main__':
    main()
