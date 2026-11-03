#pragma once
static const char API_DOCS[] PROGMEM = R"DOC(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Wireless Paper API</title>
<style>body{font:16px system-ui;max-width:850px;margin:32px auto;padding:0 18px;line-height:1.5;color:#20382b}table{border-collapse:collapse;width:100%;display:block;overflow:auto}td,th{padding:9px;border:1px solid #ccc;text-align:left}pre{padding:16px;background:#f1f3ef;white-space:pre-wrap;overflow-wrap:anywhere}code{font-size:14px;overflow-wrap:anywhere}a{color:#265745}</style>
<a href="/">← Display controls</a><h1>Wireless Paper API</h1>
<p>Control a 250 × 122 pixel black-and-white screen over HTTP on port 80. The same display JSON works over the custom LoRa packet protocol. The setup hotspot runs for the first 60 minutes after startup. Connected clients keep it open until the last one leaves. A saved always-on option is available. Normal station Wi-Fi remains enabled.</p>
<h2>Text and drawing</h2><p>Send JSON to <code>POST /api/display</code>, with <code>Content-Type: application/json</code>.</p>
<pre>{
  "text": "Hello!",
  "size": 3,
  "x": 8,
  "y": 8,
  "color": "white",
  "background": "black",
  "wrap": true,
  "clear": true
}</pre>
<table><tr><th>Setting</th><th>Values</th><th>Default</th></tr>
<tr><td>text</td><td>Text, up to 2,000 bytes. Built-in bitmap font; render a bitmap for Unicode or custom fonts.</td><td>Omitted</td></tr>
<tr><td>size</td><td>Integer 1–8. Scale of a 6 × 8 pixel font: size 2 uses a 12 × 16 pixel character cell.</td><td>2 for top-level text; 1 for text operations unless a global size is supplied</td></tr>
<tr><td>x, y</td><td>Text origin in pixels from top left. x: −500 to 750; y: −500 to 622. Off-screen pixels are clipped.</td><td>4, 4</td></tr>
<tr><td>color</td><td>"black", "white", "#000000", "#ffffff", or numeric 1 = black / 0 = white. Names are case-insensitive. Sets the default ink for text and all operations.</td><td>"black"</td></tr>
<tr><td>background</td><td>Same color values. Fills the canvas when clear is true; ignored when clear is false.</td><td>"white"</td></tr>
<tr><td>wrap</td><td>true wraps text at the screen edge; false clips long lines. Newlines start at the left edge.</td><td>true</td></tr>
<tr><td>clear</td><td>true starts with the background color; false draws over the last completed picture.</td><td>true</td></tr>
<tr><td>ops</td><td>Array of drawing operations, executed in order after top-level text. Maximum 100 operations.</td><td>No operations</td></tr></table>
<p>There are no red, green, or blue pixels. Photos can use black-and-white dithering. Large text is clipped if it does not fit; automatic font fitting is not enabled.</p>
<h2>Drawing operations</h2><p>Every operation requires <code>type</code>, <code>x</code>, and <code>y</code>. All accept <code>color</code>, which overrides the global ink. Coordinates use the same ranges as top-level x/y.</p>
<table><tr><th>Type</th><th>Additional settings</th></tr>
<tr><td>text</td><td>text (required), size (1–8), wrap (true/false)</td></tr>
<tr><td>line</td><td>x2, y2 (required end point); one pixel wide</td></tr>
<tr><td>rect</td><td>w, h (required, 1–500), fill (true for solid, default false)</td></tr>
<tr><td>circle</td><td>r (required radius, 1–250), fill (true for solid, default false)</td></tr>
<tr><td>pixel</td><td>No extra settings</td></tr></table>
<pre>{
  "clear": true,
  "background": "white",
  "color": "black",
  "ops": [
    {"type":"rect","x":0,"y":0,"w":250,"h":32,"fill":true},
    {"type":"text","x":8,"y":8,"text":"STATUS","size":2,"color":"white"},
    {"type":"text","x":8,"y":48,"text":"All systems ready","size":1},
    {"type":"circle","x":225,"y":90,"r":12,"fill":false}
  ]
}</pre>
<h2>Images</h2><p>Send exactly <strong>3,904 bytes</strong> to <code>POST /api/frame</code> with <code>Content-Type: application/octet-stream</code>. Rows are top to bottom. Each row has 32 bytes, most significant bit first; 1 means black. The last six bits in each row are padding. <code>GET /api/frame</code> returns the last completed frame. The browser controls convert pictures for you.</p>
<h2>Progress and errors</h2><p>Display writes return <code>202 {"accepted":true,"job":NUMBER}</code>. Poll <a href="/api/status">GET /api/status</a> until <code>completed_job</code> matches the job and <code>busy</code> is false. A panel timeout sets <code>display_error</code>. The server returns 409 while the screen is busy and 400 for invalid input. JSON is limited to 8,192 bytes. Rejected input does not alter the screen.</p>
<h2 id="lora-api">LoRa adapter API</h2><p>The board can send and receive raw LoRa packets as an HTTP-controlled adapter. It is not Meshtastic or LoRaWAN. Display commands still work using the WP packet format below.</p>
<p><code>GET /api/lora/status</code> returns radio state. <code>GET /api/lora/config</code> returns the saved settings and state. <code>POST /api/lora/config</code> accepts any subset of these fields; settings are saved across restarts:</p>
<pre>{"enabled":true,"frequency_mhz":868.1,"bandwidth_khz":125,"spreading_factor":7,"coding_rate":5,"sync_word":18,"power_dbm":10,"preamble":8,"crc":true,"tx_enabled":false,"duty_cycle_percent":1}</pre>
<p>Match your hardware band. Set enabled:false to deactivate the radio without losing its frequency. Set enabled:true to activate reception. Frequency 0 also disables the radio. A frequency update without an enabled field activates a positive frequency for compatibility. Accepted ranges: frequency 430–510 or 863–928 MHz; bandwidth 125/250/500 kHz; spreading factor 5–12; coding-rate denominator 5–8; sync word 0–255; TX power −9 to 22 dBm; preamble 8–65535; duty limit 0.1–100%. Header mode is explicit. TX is disabled by default. Use an appropriate antenna and settings permitted for your location.</p>
<p><code>POST /api/lora/activate</code> accepts <code>{"enabled":true}</code> or <code>{"enabled":false}</code>, and optional <code>tx_enabled</code>. A frequency must be configured before activation. Activation and TX permission are saved across restarts. Deactivating the radio does not stop station Wi-Fi or change the hotspot policy. The older <code>/api/radio</code> path remains a configuration alias.</p>
<pre>curl http://BOARD/api/lora/config -H 'Content-Type: application/json' \
  -d '{"frequency_mhz":868.1,"enabled":true,"tx_enabled":false}'

curl http://BOARD/api/lora/activate -H 'Content-Type: application/json' \
  -d '{"enabled":true,"tx_enabled":true}'

curl http://BOARD/api/lora/send -H 'Content-Type: application/json' \
  -d '{"text":"hello"}'

curl 'http://BOARD/api/lora/messages?after=0'

curl http://BOARD/api/lora/activate -H 'Content-Type: application/json' \
  -d '{"enabled":false}'</pre>
<p><code>POST /api/lora/send</code> accepts <code>{"text":"hello"}</code> or <code>{"hex":"00ff1234"}</code>, exactly one, with 1–255 payload bytes. Success returns <code>{"sent":true,"id":1,"bytes":5,"receiver_ready":true}</code>. This reports the local radio transmission, not remote receipt. Reception pauses while transmitting. An unavailable radio returns 409, disabled TX returns 403, invalid payload returns 400, and airtime pacing returns 429 with Retry-After. The pacing safeguard does not guarantee regulatory compliance.</p>
<p><code>GET /api/lora/messages?after=0</code> returns the last 16 sent/received packets in RAM, each with id, uptime_ms, direction, byte count, hex payload, printable ASCII text where possible, and RSSI/SNR for received packets. Use latest_id as the next after cursor. The truncated flag reports an expired cursor. Packet IDs and the log reset when the board restarts.</p>
<p>LoRa packets have a 16-byte little-endian header: ASCII WP (2 bytes), version 1 (1), kind 1=JSON or 2=bitmap (1), request ID (4), whole payload length (2), fragment offset (2), whole-payload CRC32 (4). Append up to 224 payload bytes. Offsets are multiples of 224; all but the final fragment contain 224 bytes. CRC matches Python zlib.crc32. Whole payloads are limited to 8,192 bytes; bitmaps are exactly 3,904. Partial transfers expire after 120 seconds of inactivity. There are no acknowledgments. Use a second LoRa transmitter, matching settings, and suitable airtime pacing.</p>
<h2>Setup hotspot</h2><p>At every startup the screen shows the hotspot name, http://192.168.4.1/, and the selected policy. The default is a 60-minute setup window from boot. If clients are connected when that hour ends, the hotspot remains on until the last client disconnects. Restarting opens a new window.</p>
<p><code>POST /api/hotspot</code> accepts <code>{"mode":"always"}</code> or <code>{"mode":"first-hour"}</code>. The selected policy survives restarts. GET /api/hotspot or /api/status reports hotspot_mode, hotspot_active, hotspot_clients, hotspot_seconds_remaining, and hotspot_held_open. The seconds remaining value describes the initial window, not a new hour started by saving a setting.</p>
<p>Only the hotspot can expire. Station Wi-Fi stays enabled. POST /api/power accepts only mode online; radio-only/off/sleep modes return 403. Legacy timed Wi-Fi display actions are rejected.</p>
<p>Wireless Paper API by Ramazan Yavuz. Provided as is, without warranty of any kind. You accept all risk from installation and use.</p>
</html>)DOC";
