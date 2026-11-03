# Security and privacy

Wireless Paper API is intended for a trusted local network and nearby users. It is firmware and a local control API, not an internet-facing service.

- The setup hotspot is deliberately open. HTTP and raw LoRa are unencrypted and unauthenticated. Nearby hotspot clients and reachable LAN clients can change the display, hotspot policy, and radio settings. Enabling radio transmit gives those clients the ability to request transmissions.
- The default hotspot window is 60 minutes after each restart. Associated clients keep it open after the deadline until the last client disconnects. The always-on policy is opt-in and persists across restarts. This limits idle exposure, but does not authenticate users.
- Do not forward port 80 from the internet. No NAT or internet routing is enabled by this firmware.
- Wi-Fi credentials belong only in a private, ignored `include/secrets.h`. The example contains placeholders. Without that file, the public source builds with no station credentials and provides the setup hotspot.
- There is no HTTP endpoint to submit or retrieve a Wi-Fi password. Configure station credentials locally and rebuild. Do not submit a network password over the open hotspot.
- Private firmware binaries and flash dumps can contain Wi-Fi credentials. Do not upload them, commit them, or attach them to bug reports. Device flash is not encrypted by this project.
- Radio TX is disabled by default. Match the antenna and purchased frequency band, choose settings permitted at your location, and enable TX explicitly. The configurable airtime pacing guard is not a regulatory compliance system.
- Packet logs hold the last 16 packets in RAM and are visible to API clients. Do not send secrets over the raw LoRa link. Logs and request IDs reset on restart.
- WP display fragments have length checks, bounded assembly, and CRC32. CRC detects damage; it is not authentication. Any compatible transmitter can submit a display command.
- The same transceiver cannot receive while transmitting. No acknowledgment proves that another device received a sent packet.

Tests cover parsing, buffer boundaries, packet assembly, hotspot policy boundaries, rendering, and local radio initialization. They do not establish radio range, encryption, battery life, or regulatory approval. Over-the-air interoperability needs a second compatible radio.

Provided as is, without warranty of any kind. You accept all risk from installation and use.
