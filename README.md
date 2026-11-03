# Wireless Paper API

Control a Heltec Wireless Paper e-paper screen from a browser, Python, HTTP, or raw LoRa. Paint on a canvas, send text with layout settings, upload a picture, or use the board as a bidirectional LoRa adapter.

A personal open-source project by **Ramazan Yavuz**. [Project page](https://ra-yavuz.github.io/wireless-paper-api/) · [API reference](docs/api.html) · [Security notes](SECURITY.md)

## What it does

- Displays black-and-white content at 250 × 122 pixels.
- Provides a browser canvas with a pen, eraser, brush size, undo/redo, picture import, and **Show canvas**.
- Accepts text, positions, font scale, ink/background colors, wrapping, and drawing operations through JSON.
- Offers full-screen bitmap upload and readback.
- Runs its web controls and HTTP API on standard port 80.
- Opens an unsecured setup hotspot for the first **60 minutes after each startup**. If clients are still associated, it stays on until the last client disconnects.
- Saves a choice between the default first-hour policy and **always on** across restarts.
- Shows the hotspot name, web URL, and selected policy on the screen at startup.
- Keeps station Wi-Fi enabled after the hotspot window ends. The two connections are separate.
- Exposes raw LoRa send/receive, saved modem configuration, and a rolling packet log over HTTP. LoRa display commands use the same JSON and bitmap formats.

The default build targets **Wireless Paper V1.1**. V1.1.1/V1.2 have another panel: change `PAPER_REVISION=11` to `PAPER_REVISION=12` before building. Check the actual board and screen revision. The LoRa frequency must match the purchased radio band.

## Install

You need Python, a USB data cable, and the board. Linux, macOS, and Windows can use PlatformIO; select the serial port for your system. No Debian package is supplied.

```sh
git clone https://github.com/ra-yavuz/wireless-paper-api.git
cd wireless-paper-api
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/pio run
.venv/bin/pio run -t upload --upload-port /dev/ttyUSB0
```

On Windows, activate the virtual environment using its `Scripts` directory and select a port such as `COM3` instead. Do not flash an unrelated board or panel revision.

**The public source contains no Wi-Fi credentials.** Without a private configuration file, the firmware starts the hotspot and does not request a station connection. To join your own network, copy the example locally:

```sh
cp include/secrets.example.h include/secrets.h
```

Edit the ignored `include/secrets.h`:

```cpp
#pragma once
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

Then rebuild and upload. Never commit this file. A firmware binary built with real credentials can contain them too. Do not publish that binary or a flash dump. Station credentials are configured locally, not submitted through the open HTTP hotspot.

Before replacing existing firmware, consider saving a private backup:

```sh
mkdir -p .local
.venv/bin/esptool --port /dev/ttyUSB0 read-flash 0 ALL .local/original-flash.bin
```

To restore a backup for this same board:

```sh
.venv/bin/esptool --port /dev/ttyUSB0 write-flash 0 .local/original-flash.bin
```

No device backups, credentials, or private firmware binaries are included in the repository.

## Connect and paint

After startup, read the e-paper screen. Join `WirelessPaper-<suffix>` and open **http://192.168.4.1/**. The suffix is derived from the board's hardware address. In the **Hotspot** section, choose **On for the first 60 minutes** or **Keep it on permanently**, then save. The setting survives restarts.

In **Canvas**, paint with a mouse or finger. Choose a black pen or white eraser, adjust the brush size, and use undo/redo when needed. **Show canvas** sends the drawing to the device. You can also start with an imported picture or load the last completed screen image. Editing the canvas does not change the paper screen until you send it.

If station Wi-Fi is configured, the same controls are available at the LAN address assigned by your router and at `http://wireless-paper-<suffix>.local/` where multicast DNS is supported. The DHCP client restores its previous lease when possible; the router still decides which IP is available. Reserve the station MAC in your router for a guaranteed address. Read the address and MAC from **Device information**.

The first-hour window is measured from startup, not from the last HTTP request or from saving a setting. A connected client keeps it open after the deadline. When the last client leaves, the default policy closes the hotspot. Restarting the device opens a fresh setup window even if the saved station network is forgotten or unavailable. Normal station Wi-Fi continues after the hotspot stops.

## HTTP display API

Open `/api` on the device for the full reference. The same reference is in [docs/api.html](docs/api.html). All endpoints use port 80.

| Method and path | Purpose |
| --- | --- |
| `GET /` | Browser controls and canvas |
| `GET /api` | API reference |
| `GET /api/status` | Connections, hotspot policy, display jobs, radio state |
| `POST /api/display` | Text and drawing JSON |
| `POST /api/frame` | Full binary bitmap |
| `GET /api/frame` | Last successfully completed bitmap |
| `GET/POST /api/hotspot` | Read/save `first-hour` or `always` policy |
| `GET/POST /api/radio` | Read/save modem settings |
| `POST /api/lora/send` | Send one raw radio packet |
| `GET /api/lora/messages` | Read the rolling sent/received packet log |
| `POST /api/power` | Accepts `online` only; full Wi-Fi shutdown modes return 403 |

Text example, replacing `BOARD` with your device's address:

```sh
curl http://BOARD/api/display -H 'Content-Type: application/json' \
  -d '{"text":"Hello!","size":3,"x":8,"y":8,"color":"white","background":"black","wrap":true}'
```

| Setting | Values | Default |
| --- | --- | --- |
| `text` | Up to 2,000 bytes; built-in bitmap font | Omitted |
| `size` | Integer 1–8; a 6 × 8 pixel cell at size 1 | 2 for top-level text; 1 for text operations without a global size |
| `x`, `y` | Pixel position from top left; off-screen pixels clip | 4, 4 for top-level text; required for operations |
| `color` | `black`, `white`, `#000000`, `#ffffff`, or numeric 1 = black / 0 = white | Black |
| `background` | Same color values; used when `clear:true` | White |
| `wrap` | Boolean; wrap long lines at the screen edge | true |
| `clear` | Boolean; false draws over the last completed frame | true |
| `ops` | Up to 100 drawing operations | None |

Operations support `text`, `line`, `rect`, `circle`, and `pixel`. All take `x`, `y`, and optional `color`. Lines require `x2`, `y2`; rectangles require `w`, `h`; circles require `r`. Rectangles and circles accept `fill`. Text operations accept their own `text`, `size`, and `wrap` and otherwise inherit global settings. See the reference for ranges and examples. Built-in newlines start at the left screen edge. Render a bitmap for Unicode text or custom fonts. This panel has no color pixels and is not suitable for smooth video.

Writes return HTTP **202** with a job ID. Acceptance does not mean the refresh has completed. Poll `/api/status` until `completed_job` matches the ID and `busy` is false. A panel timeout sets `display_error`. New display writes while busy return **409**; invalid requests return **400**. JSON is limited to 8,192 bytes.

A full bitmap is **3,904 bytes**: 122 rows of 32 bytes, MSB first, **1 = black**. The last six bits of each row are padding. POST with `Content-Type: application/octet-stream`. The startup banner replaces the previous displayed content on restart; preview and packet history are held in RAM.

## Python client and reusable helpers

```sh
.venv/bin/python tools/paper.py --url http://BOARD status
.venv/bin/python tools/paper.py --url http://BOARD text Hello --size 3 --color white --background black
.venv/bin/python tools/paper.py --url http://BOARD image picture.png
.venv/bin/python tools/paper.py --url http://BOARD json drawing.json
.venv/bin/python tools/paper.py --url http://BOARD send 'hello over LoRa'
.venv/bin/python tools/paper.py --url http://BOARD send 00ff1234 --hex
.venv/bin/python tools/paper.py --url http://BOARD receive --after 0
.venv/bin/python tools/paper.py --url http://BOARD radio modem.json
```

`tools/paper.py` also exposes `image_frame(path)` and `packets(body, kind, request_id)` for use from other Python code. Image conversion fits the source inside the display and dithers it to black and white. The device does not need a cloud account or hosted service.

## LoRa adapter

TX is disabled and the frequency is zero by default. Attach an appropriate antenna, confirm the purchased frequency band, and configure permitted settings before enabling TX. Example configuration only:

```json
{
  "frequency_mhz": 868.1,
  "bandwidth_khz": 125,
  "spreading_factor": 7,
  "coding_rate": 5,
  "sync_word": 18,
  "power_dbm": 10,
  "preamble": 8,
  "crc": true,
  "tx_enabled": false,
  "duty_cycle_percent": 1
}
```

Send this to `POST /api/radio`. Partial updates are supported and saved across restarts. Frequency 0 disables the radio. Supported ranges are documented in `/api`. The coding rate is expressed as the denominator of 4/5 through 4/8. Header mode is explicit. Enable `tx_enabled` separately when ready to transmit.

`POST /api/lora/send` takes exactly one of `text` or `hex`, with 1–255 payload bytes. It returns local transmit success, not proof that a remote device received the packet. The same transceiver cannot listen while transmitting. Received packets are logged and are not automatically retransmitted.

`GET /api/lora/messages?after=0` returns the last 16 RX/TX packets in RAM, with IDs, uptime timestamps, hex, printable ASCII where possible, and RSSI/SNR for received packets. Use `latest_id` as your next cursor. Restarting resets the log and IDs.

An airtime pacing guard defaults to 1%. HTTP 429 and `Retry-After` indicate when a send must wait. This is a configurable safeguard, not regulatory approval or a complete duty-cycle compliance system. Resetting the device resets its pacing history. Follow the radio rules for your location and hardware.

## Sending display commands over LoRa

This is a custom raw LoRa protocol, not Meshtastic or LoRaWAN. Both radios need matching modem settings. Generate packets for any compatible sender:

```sh
.venv/bin/python tools/paper.py --lora-out /tmp/paper-packets text 'Remote screen'
```

Each `packet-NNN.bin` is one payload. Send files listed in `manifest.json`, not stale files left by a previous transfer. A full image takes 18 packets. With a second board acting as an HTTP-to-LoRa adapter, the client can send the fragments for you and wait for its pacing guard:

```sh
.venv/bin/python tools/paper.py --url http://ADAPTER --via-lora text 'Remote screen'
.venv/bin/python tools/paper.py --url http://ADAPTER --via-lora image picture.png
```

Image transfers can be slow because of airtime limits. No acknowledgment confirms delivery; resend the whole command if packets are lost. Radio sending, receiving, and remote display control remain distinct operations.

Packet format, little endian:

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 2 | ASCII `WP` |
| 2 | 1 | Version 1 |
| 3 | 1 | Kind: 1 = JSON, 2 = bitmap |
| 4 | 4 | Request ID |
| 8 | 2 | Complete payload size, 1–8,192 |
| 10 | 2 | Fragment offset, a multiple of 224 |
| 12 | 4 | Whole-payload CRC32, compatible with Python `zlib.crc32` |
| 16 | 1–224 | Fragment data |

All but the final fragment contain 224 bytes. The maximum radio payload is 240 bytes. Fragments may arrive out of order; duplicates are ignored. A partial transfer expires after 120 seconds without a matching valid fragment. A live transfer cannot be replaced by another one. Length/CRC checks prevent partial or damaged data from reaching the screen. The last eight accepted request IDs are deduplicated in RAM. CRC is not authentication.

## Power and limitations

The CPU runs at 80 MHz, the LED is off, Wi-Fi modem power saving is requested, and the display controller sleeps after refresh. Station Wi-Fi remains available when configured. The hotspot can expire according to its policy, which can reduce Wi-Fi overhead. There is no whole-device deep-sleep mode that could strand HTTP clients. Actual current and battery life have not been measured.

Native tests cover the actual fragment assembler, CRC, bounds, duplicates, ordering, corruption, 100,000 malformed packets, exact hotspot timing boundaries, connected-client hold, long uptimes, radio setting limits, and pacing arithmetic. Live board checks cover text/image pixel results, browser controls, radio initialization/configuration, saved hotspot policy across resets, and rejected send requests without RF transmission.

**A second radio is needed to validate actual over-the-air transmission, reception, range, and interoperability.** Local driver initialization and API checks do not prove those outcomes. A real 60-minute expiration run and associated-client behavior are separate hardware checks; native policy tests do not substitute for a measured battery or field test.

```sh
mkdir -p .local
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Iinclude tests/protocol_test.cpp -o .local/protocol-test
.local/protocol-test
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Iinclude tests/policy_test.cpp -o .local/policy-test
.local/policy-test
.venv/bin/python -m unittest discover -s tests -v
```

The optional `tests/hardware_check.py --url http://BOARD --radio-mhz FREQUENCY` changes the screen and briefly changes modem settings, restoring them afterward. Use only with your own board and confirmed band. It does not transmit valid RF packets.

## License and disclaimer

MIT, by Ramazan Yavuz. Provided **as is, without warranty of any kind**. You accept all risk from installation and use, including damage, data loss, disclosure, or unintended radio operation. Read [SECURITY.md](SECURITY.md) and the hardware documentation before flashing.

Hardware references: [Heltec Wireless Paper](https://heltec.org/project/wireless-paper/), [panel revisions](https://docs.heltec.org/en/node/esp32/wireless_paper/hardware_update_log.html), [display driver](https://github.com/todd-herbert/heltec-eink-modules), [RadioLib](https://github.com/jgromes/RadioLib).
