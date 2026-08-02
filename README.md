# RemoteDoorBell (ESP-NOW + AES-128)

A wireless doorbell system using ESP8266 microcontrollers (NodeMCU/ESP-01) with AES-128 encrypted ESP-NOW communication. Designed for ultra-low latency and ultra-low power using a **Fire-and-Forget** architecture.

## ⭐ Highlights

* 🚀 **ESP-NOW Protocol**: Lightweight, mesh-capable wireless communication—no WiFi router needed!
* ⚡ **Fire-and-Forget**: Zero handshake overhead. The remote fires back-to-back packets and instantly powers down, resulting in sub-200ms latency from button press to chime.
* 💡 **Optimized for Tiny Hardware**: Fully functional on ESP-01 & ESP8266 with only 512KB flash.
* 🛡️ **Secure Communication**: AES-128 ECB encryption + rolling counter replay protection.
* 🧠 **Crash-Proof Receiver**: Uses safe memory alignment (`memcpy`) to prevent ESP8266 `Exception 9` hardware faults.

## Features

* 🔐 **Secure**: AES-128 ECB encryption for all messages.
* 📡 **Wireless**: ESP-NOW protocol (no WiFi network required).
* 🔘 **Multi-remote**: Support up to 8 paired remote transmitters.
* 🔧 **Flexible output**: Relay, passive buzzer (ding-dong), or active buzzer modes.
* 🪫 **Low power**: Remote draws 0µA when idle (physically disconnected via VCC button).
* 💾 **Persistent**: Stores paired remotes and rolling counters in EEPROM.
* 🛡️ **Replay protection**: Rolling counter with duplicate/rollback suppression.

## System Overview

This project is split into two firmware roles:

1. **Receiver (`src/receiver_main.cpp`)**: Listens for encrypted ESP-NOW ring packets, stores paired remotes, validates the rolling counter, suppresses duplicates, and triggers a relay or buzzer.
2. **Remote (`src/remote_main.cpp`)**: Powers on via a physical VCC button, instantly increments and saves its rolling counter, fires 3 back-to-back encrypted packets, and powers off when the button is released.

### Message Flow (Fire-and-Forget)

1. User presses the doorbell button, physically applying power to the Remote ESP-01.
2. Remote boots, immediately increments its rolling counter, and saves it to EEPROM (preventing desync if power is cut early).
3. Remote encrypts a 16-byte `RING` packet and blasts it 3 times over ESP-NOW.
4. User releases the button; Remote loses power.
5. Receiver catches the first packet, decrypts it, verifies the sender/counter, triggers the chime, and ignores the subsequent 2 duplicate packets.

## Important Configuration Notes

* `PLAINTEXT_DEBUG` must match in both `src/receiver_main.cpp` and `src/remote_main.cpp` (Set to `0` for production).
* `RELAY_ACTIVE_HIGH` in `src/receiver_main.cpp` selects relay polarity when `OUTPUT_MODE` is `RELAY`.
* `receiverMac[]` in `src/remote_main.cpp` must be updated to the receiver’s MAC address after flashing the receiver.

## Hardware

### Receiver (Required)
* **ESP-12E/F** (NodeMCU v2/v3, Lolin, D1 Mini) — Recommended (4MB flash)
* **OR ESP-01** (512KB flash) — Fully supported on tiny devices ⚡

### Remote (Required)
* **ESP-01** (512KB flash) — Battery-powered transmitter 🔋
* **Push Button**: Wired in series with the main VCC power line.
* **Capacitor (Highly Recommended)**: 100µF - 470µF across VCC and GND to keep the ESP-01 alive long enough to finish EEPROM writes if the button is released quickly.

## Quick Start

### 1. Flash Receiver First
```bash
pio run -e receiver_12e -t upload   # NodeMCU/ESP-12E
# or
pio run -e receiver_01 -t upload    # Bare ESP-01 (512KB flash)