# RemoteDoorBell Architecture

## Overview

This project consists of two ESP8266 roles:

- **Receiver** (`src/receiver_main.cpp`): receives encrypted ring packets, validates them, triggers a relay or buzzer, and sends an ACK.
- **Remote** (`src/remote_main.cpp`): boots on button press, sends a ring packet, waits for an ACK, then deep sleeps to conserve battery.

The communication path uses ESP-NOW with a shared AES-128 key and a rolling counter for replay protection.

## Packet structure

Each packet is exactly 16 bytes:

- `uint8_t type` — `1` for RING, `2` for ACK
- `uint32_t sender` — sender chip ID
- `uint32_t counter` — rolling counter
- `uint8_t pad[7]` — padding for AES block size

RING packets and ACK packets share the same structure. The receiver echoes the sender ID in its ACK.

## Receiver behavior

1. Initialize serial/debug output.
2. Configure output pin and pairing button.
3. Set initial output idle state:
   - Relay: `RELAY_OFF`
   - Buzzer: `LOW`
4. Initialize AES context, EEPROM, and ESP-NOW.
5. Register persisted remote peers so ACKs can be sent.
6. Listen for incoming ESP-NOW packets.

### onReceive()

- Reject packets of unexpected length.
- Decrypt packet unless `PLAINTEXT_DEBUG == 1`.
- Ignore non-`RING` packets.
- If sender is unknown and pairing is enabled, register the remote.
- Calculate `delta = packet.counter - remotes[idx].lastCounter`.
- Suppress duplicates and suspicious rollback attempts.
- Trigger the output and set `ringing = true`.
- Send an ACK back to the remote.

### Output logic

- `OUTPUT_MODE_RELAY` energizes the relay with `RELAY_ON` and releases it after `RING_DURATION`.
- `OUTPUT_MODE_BUZZER_TONE` plays a ding-dong sequence synchronously.
- `OUTPUT_MODE_BUZZER_SIMPLE` turns the buzzer on for `RING_DURATION` and then off.

### Relay polarity

The receiver supports explicit relay polarity selection via `RELAY_ACTIVE_HIGH` in `src/receiver_main.cpp`.

- `RELAY_ACTIVE_HIGH = 0` **(default)** — standard active-LOW modules (SRD-05VDC, HY-SRD).
  LOW energizes the coil; HIGH releases it. The module's onboard optocoupler pull-up holds IN HIGH
  during the ESP boot window, so the relay cannot fire before firmware initialises the GPIO.
- `RELAY_ACTIVE_HIGH = 1` — active-HIGH relay modules (uncommon).

### Boot-time output safety

The output pin is initialised at the very top of `setup()`, before Serial, WiFi, or any other
call that could delay execution. The order is `digitalWrite(RELAY_OFF)` → `pinMode(OUTPUT)`,
not the reverse. This pre-loads the ESP8266 output latch with the safe idle level before the
push-pull driver is enabled, preventing a glitch pulse on the relay coil.

The boot-ready "two clicks" signal has been removed for `OUTPUT_MODE_RELAY`. Pulsing the relay
at the end of `setup()` was the primary cause of the doorbell ringing 2–3 times on every
power-up. Buzzer modes retain the two-tone ready signal. Pairing-mode entry/exit clicks are
unaffected.

## Remote behavior

1. Configure GPIO2 as an LED indicator.
2. Initialize AES and EEPROM.
3. Load the last saved counter from EEPROM.
4. Build a `RING` packet and encrypt it unless `PLAINTEXT_DEBUG == 1`.
5. Initialize ESP-NOW and add the receiver peer.
6. Send the packet with retries, waiting for an ACK each time.
7. Blink the LED for success/failure.
8. Save the counter and deep sleep.

### Power and wake strategy

The remote uses an ESP-01 with the button wired to `RST`.
Each button press causes a hard reset, sending one ring packet and then entering deep sleep.
This strategy minimizes idle current.

## Hardware wiring notes

### Receiver

- NodeMCU / ESP-12E: use `D5` for `OUTPUT_PIN`, `D2` for pairing button.
- ESP-01 receiver: use `GPIO2` for `OUTPUT_PIN`, `GPIO0` for pairing button.

### Remote

- RST ↔ button ↔ GND
- Use a 10K pull-up on RST to VCC.
- GPIO0 must remain pulled up or floating to avoid bootloader mode.
- GPIO2 is used for an active-LOW status LED.

## Suggested improvements

### Software

- Move from blocking `delay()` patterns to a non-blocking state machine for pairing and tone output.
- Add remote management commands in the receiver to remove or re-register remotes.
- Check `esp_now_add_peer()` return values and recover on peer registration failure.
- Consider stronger authentication than AES-ECB if the protocol evolves, e.g. AES-GCM or HMAC.
- Add a build-time or runtime configuration option for the receiver MAC rather than hardcoding it in `remote_main.cpp`.

### Hardware

- Use a transistor/driver stage if the relay module requires more current than the ESP GPIO can safely provide.
- Add a small status LED or buzzer to the receiver to indicate pairing and fault conditions.
- Use proper 3.3V regulation and decoupling capacitors on both boards.
- For the remote, consider a battery voltage monitor and low-battery indication.

## Quick validation checklist

- Ensure `PLAINTEXT_DEBUG` is `0` in both firmware files for production.
- Ensure `receiverMac[]` in `src/remote_main.cpp` matches the boot-time receiver MAC.
- Set `RELAY_ACTIVE_HIGH` correctly for your relay module.
- Verify the receiver output pin is correct for the board in use.
- Verify pairing button wiring is active LOW with a pull-up.