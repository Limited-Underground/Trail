# Heltec V4.2 direct-radio diagnostic

This is a separate, identical-node ESP-IDF diagnostic for the two Heltec WiFi
LoRa 32 V4.2 bench boards. It does not change or share application code with
`heltec_v4_bench`.

The image initializes the SX1262 in receive mode and never transmits at boot.
The USB Serial/JTAG console accepts:

- `status`
- `rx`
- `arm` (one transmission, expires after 30 seconds)
- `send <A|B> <session> <A>B|B>A> <sequence> <fill:1..163>`

The fixed close-bench profile is 915.000 MHz, 125 kHz bandwidth, SF7, CR 4/5,
private sync word `0x12`, explicit headers, CRC, LDRO disabled, eight preamble symbols, and 2 dBm radio output. The sync word is only a packet discriminator; it is not encryption. Frames carry a nonzero session, consistent sender role/direction, sequence, and 1..163 bytes of deterministic fill. Received payload bytes are validated but never logged. The
target pins match Heltec's `WIFI_LORA_32_V4` GC1109 configuration. RadioLib is
pinned through the ESP-IDF component manager to version 7.7.1.

This target is source/build evidence only until both physical boards are
flashed and a privacy-safe bidirectional packet test is recorded. It is not a
supported-hardware or regulatory-acceptance claim.
