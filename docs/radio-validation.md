# Two-board radio validation

Record: WPA-RF-2026-11-03. Firmware: 1.4.0. Author: Ramazan Yavuz.

The HTTP-to-LoRa adapter delivered exact payloads in both directions between a Heltec Wireless Paper and a Heltec Wireless Tracker using the Tracker V1.1 build. This is a local two-board test. It establishes API-to-radio-to-API delivery with the modem settings below. It does not establish range, battery life, delivery guarantees, or compatibility with Meshtastic or LoRaWAN.

## Objective and scope

Check whether an HTTP client can transmit bytes through one board, read those same bytes at the peer, reply, and use temporary modem settings without replacing the configured receiver. The test covers raw LoRa and HTTP APIs on these two connected boards. Long-distance links, other radio networks, physical display inspection, and a full hour-long hotspot run are excluded.

## Settings and method

Both boards used 868.1 MHz, 125 kHz bandwidth, spreading factor 7, coding rate 4/5, sync word 18, an eight-symbol preamble, packet CRC, explicit headers, -9 dBm transmit power, and one-percent pacing. The temporary profile changed bandwidth to 250 kHz and spreading factor to 8 at the receiving peer and for one outgoing request.

`tests/radio_pair_check.py` submitted each payload through `POST /api/lora/send`, then polled the peer's `GET /api/lora/messages` log after its previous packet ID. Passing required an RX entry with exactly matching hexadecimal bytes, rather than a local successful-send response. The script restored the saved settings on both boards afterward.

## Results

| Check | Observation |
| --- | --- |
| Paper to Tracker | Exact 25-byte message received |
| Tracker to Paper | Exact 25-byte reply received |
| Binary payload with zero bytes | All eight bytes matched |
| Maximum raw payload | All 255 bytes matched |
| Temporary modem profile | Exact 30-byte packet received using 250 kHz / SF8 |
| Receiver restoration | Exact 26-byte reply received after returning to 125 kHz / SF7 |
| One-shot send from inactive baseline | Exact 36-byte packet received; sender returned to inactive state |
| Pacing | Immediate extra send rejected with HTTP 429 and Retry-After |
| Invalid overrides | Null, out-of-range, and unknown settings rejected with HTTP 400; saved profile unchanged |

Received signal strength ranged from -119 to -110 dBm during this run. Those readings describe this setup and do not establish antenna suitability or link margin for another installation.

## Evidence and limits

The [machine-readable record](radio-pair-2026-11-03.json) contains the seven send responses, matching peer RX entries, modem profiles, signal measurements, and test outcomes. It contains no Wi-Fi credentials or device network addresses.

SHA-256 of that record: `a4b26757115a10956f1f2419aa0560ab370baa376c74a25f909d2c4cd1f60e4a`.

Proven: exact local bidirectional raw packet delivery and the profile behavior listed above. Partially proven: suitability as an adapter for applications using these settings, limited to this pair and run. Not proven: field range, packet-loss rate over sustained use, foreign protocol interoperability, physical screen appearance, measured power, or a real 60-minute hotspot expiration run.

The final installed builds also passed a browser test at 0 dBm: each board sent a message using the web controls, and the peer's open packet log refreshed with the exact message. Canvas painting, undo/redo, and exact bitmap readback passed for both native screen sizes. The [browser record](browser-pair-2026-11-03.json) contains those outcomes. One earlier browser transmission at -9 dBm produced a receive error and no received packet; the higher-power browser test passed. This reinforces the limit on delivery claims without acknowledgments.

A local `sent:true` response by itself remains a transmit result. Applications that require delivery confirmation must implement acknowledgments and retries.

Provided as is, without warranty of any kind. You accept all risk from installation and use.
