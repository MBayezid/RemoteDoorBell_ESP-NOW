
---

### 2. `docs/ARCHITECTURE.md`

```markdown
# RemoteDoorBell Architecture

## Overview
This project consists of two ESP8266 roles communicating via a **Fire-and-Forget** UDP-like paradigm over ESP-NOW. 
* **Receiver (`src/receiver_main.cpp`)**: Continuously listens for encrypted ring packets, validates them, suppresses duplicates, and triggers a relay or buzzer.
* **Remote (`src/remote_main.cpp`)**: Powered directly via a physical push-button on the VCC line. Boots, increments its rolling counter, blasts redundant packets, and powers off.

The communication path uses ESP-NOW with a shared AES-128 key and a rolling counter for replay protection. No ACK handshakes are used, ensuring ultra-low latency and allowing the remote to power down instantly.

## Packet Structure
Each packet is exactly 16 bytes (matching the AES-128 block size):
* `uint8_t type` — `1` for RING (Type 2 is reserved/unused).
* `uint32_t sender` — sender chip ID.
* `uint32_t counter` — rolling counter.
* `uint8_t pad[7]` — padding for AES block size.

## Receiver Behavior
1. Initialize output pin to a safe idle state *before* enabling the GPIO driver (prevents boot glitches).
2. Configure pairing button, AES context, EEPROM, and ESP-NOW.
3. Register persisted remote peers from EEPROM.
4. Listen for incoming ESP-NOW packets.

### `onReceive()` Callback
1. Reject packets of unexpected length.
2. **Memory Alignment Fix**: Use `memcpy` to safely copy the decrypted byte array into a `Packet` struct. (Direct pointer casting causes ESP8266 `Exception 9` LoadStoreAlignmentCause crashes).
3. Decrypt packet (unless `PLAINTEXT_DEBUG == 1`).
4. Ignore non-RING packets.
5. If sender is unknown and pairing is enabled, register the remote.
6. Calculate `delta = packet.counter - remotes[idx].lastCounter`.
7. Suppress exact duplicates (`delta == 0`) and suspicious rollbacks (`delta > 0xF0000000`).
8. Trigger the output and set `ringing = true`.
9. Return immediately (No ACK sent).

## Remote Behavior
1. Power is applied via the VCC push-button.
2. Configure GPIO2 (LED) and initialize AES/EEPROM.
3. **Increment Early Strategy**: Load the counter, increment it, and save it to EEPROM *immediately* (with a 50ms delay to ensure flash completion). This prevents desync if the user releases the button before TX completes.
4. Build a `RING` packet and encrypt it.
5. Initialize ESP-NOW and add the receiver peer.
6. Fire-and-Forget: Send the packet 3 times back-to-back with a 30ms delay to overcome RF collisions without waiting for ACKs.
7. Enter deep sleep (or simply lose power when the button is released).

## Power and Wake Strategy
The remote uses an ESP-01 with a push-button wired in series with the main **VCC power line**. 
* **Idle Current**: True 0µA (physically disconnected).
* **Active Time**: ~150ms per press.
* **Hardware Mitigation**: Because the ESP8266 requires time to boot and write to EEPROM, a **100µF to 470µF capacitor** across VCC and GND is highly recommended. This acts as a temporary battery, keeping the chip alive long enough to finish the `EEPROM.commit()` if the user releases the button too quickly.

## Hardware Wiring Notes

### Receiver
* **NodeMCU / ESP-12E**: `D5` for `OUTPUT_PIN`, `D2` for pairing button.
* **ESP-01 receiver**: `GPIO2` for `OUTPUT_PIN`, `GPIO0` for pairing button.

### Remote
* **VCC**: Push-button to Power Source (e.g., CR2032 or LiPo).
* **Capacitor**: 100µF+ across VCC and GND.
* **GPIO0**: 10K pull-up to VCC (prevents bootloader mode).
* **GPIO2**: Status LED (active LOW).

## Suggested Improvements

### Software
* Move from blocking `delay()` patterns to a non-blocking state machine for pairing and tone output in the receiver.
* Add remote management commands in the receiver to remove or clear the EEPROM whitelist.
* Consider stronger authentication than AES-ECB if the protocol evolves (e.g., AES-GCM or HMAC).

### Hardware
* Use a transistor/driver stage if the relay module requires more current than the ESP GPIO can safely provide.
* Add a battery voltage monitor to the remote to indicate low-battery conditions via the LED.

## Quick Validation Checklist
* [ ] Ensure `PLAINTEXT_DEBUG` is `0` in both firmware files for production.
* [ ] Ensure `receiverMac[]` in `src/remote_main.cpp` matches the boot-time receiver MAC.
* [ ] Set `RELAY_ACTIVE_HIGH` correctly for your relay module.
* [ ] Verify the receiver output pin is correct for the board in use.
* [ ] Verify a capacitor is installed on the Remote VCC/GND lines.