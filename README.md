# 🚪 RemoteDoorBell — Complete Documentation

> **ESP-NOW + AES-128 Encrypted Wireless Doorbell**
> Fire-and-Forget Architecture | Zero Standby Power | Sub-200ms Latency

---

## 📹 Demo & Setup Guide

> 🎬 **Video Tutorial:** [YouTube Link — Coming Soon]
>
> *Covers: Hardware assembly, flashing firmware, pairing remotes, and live demo.*

---

## 📑 Table of Contents

1. [What Is This?](#1-what-is-this)
2. [How It Works](#2-how-it-works)
3. [Hardware — Receiver](#3-hardware--receiver)
4. [Hardware — Remote](#4-hardware--remote)
5. [Wiring Diagrams](#5-wiring-diagrams)
6. [Software Setup & Flashing](#6-software-setup--flashing)
7. [Configuration Reference](#7-configuration-reference)
8. [Pairing a Remote](#8-pairing-a-remote)
9. [Security & Encryption](#9-security--encryption)
10. [Troubleshooting](#10-troubleshooting)
11. [Known Fixes & Engineering Notes](#11-known-fixes--engineering-notes)

---

## 1. What Is This?

A **wireless doorbell** built with cheap ESP8266 chips. No WiFi router needed. No cloud. No subscription.

| Feature | Detail |
|---|---|
| **Protocol** | ESP-NOW (peer-to-peer, no router) |
| **Encryption** | AES-128 ECB + rolling counter |
| **Latency** | < 200 ms (button press → chime) |
| **Remote standby power** | **0 µA** (physically disconnected) |
| **Multi-remote** | Up to 8 paired transmitters |
| **Output options** | Relay / Passive buzzer (ding-dong) / Active buzzer |
| **Smallest supported board** | ESP-01 (512 KB flash) |

### The Two Roles

| Role | Board | Power | Job |
|---|---|---|---|
| **Receiver** | NodeMCU / ESP-12F / ESP-01 | Always-on (USB or wall adapter) | Listens, validates, rings the chime |
| **Remote** | ESP-01 | Battery, switched by a button | Boots → fires 3 packets → dies |

---

## 2. How It Works

### Fire-and-Forget Message Flow

```
 USER PRESSES BUTTON
        │
        ▼
 ┌─────────────────────────────────────────────────┐
 │  REMOTE (ESP-01) powers on via VCC button       │
 │                                                 │
 │  1. Boot                                        │
 │  2. Increment rolling counter                   │
 │  3. Save counter to EEPROM  ← (done FIRST)      │
 │  4. Build 16-byte RING packet                   │
 │  5. Encrypt with AES-128                        │
 │  6. Blast packet ×3 over ESP-NOW (30 ms apart)  │
 │  7. Deep sleep / power off                      │
 └─────────────────────────────────────────────────┘
        │  (RF packets)
        ▼
 ┌─────────────────────────────────────────────────┐
 │  RECEIVER (always listening)                    │
 │                                                 │
 │  1. Catch first packet                          │
 │  2. Decrypt                                     │
 │  3. Verify sender is in whitelist               │
 │  4. Check rolling counter (reject duplicates)   │
 │  5. Trigger relay / buzzer  🔔                  │
 │  6. Ignore packets 2 & 3 (duplicates)           │
 └─────────────────────────────────────────────────┘
```

> **Why 3 packets?** The remote has no way to know if the receiver got the message (no ACK). Sending 3 copies with the same counter guarantees delivery. The receiver's duplicate suppression ignores copies 2 and 3.

> **Why save the counter FIRST?** If the user releases the button too fast, power dies mid-boot. By saving the counter *before* doing anything else, the next press always sends a *new* counter — preventing a permanent "duplicate" lockout.

---

## 3. Hardware — Receiver

### Recommended Boards

| Board | Flash | Notes |
|---|---|---|
| **NodeMCU v2 / v3** | 4 MB | ✅ Easiest — built-in USB, breadboard-friendly |
| **Wemos D1 Mini** | 4 MB | ✅ Compact, USB |
| **ESP-12F (bare)** | 4 MB | Needs USB-to-serial adapter |
| **ESP-01 / ESP-01S** | 512 KB | ⚡ Works, but needs adapter for flashing |

### Receiver BOM

| Qty | Component | Notes |
|---|---|---|
| 1 | ESP-12F / NodeMCU / ESP-01 | Receiver MCU |
| 1 | Relay module **OR** buzzer | Output device (see config) |
| 1 | Tactile push button | Pairing button (to GND) |
| 1 | 10 kΩ resistor | Pull-up for pairing button *(not needed if using internal pull-up)* |
| 1 | Micro-USB cable | Power + flashing |

### Receiver Pin Mapping (NodeMCU)

| Function | Pin | Connection |
|---|---|---|
| Output (relay/buzzer) | **D5** (GPIO14) | Relay signal / buzzer (+) |
| Pairing button | **D2** (GPIO4) | Button → GND (active LOW) |

> For **ESP-01 receiver**: change `OUTPUT_PIN` to `2` and `PAIRING_BTN_PIN` to `0` in the code.

### Receiver Wiring Photo

> 📷 *[Insert photo: `docs/receiver_wiring.jpg`]*
>
> *NodeMCU with relay module on D5 and pairing button on D2.*

---

## 4. Hardware — Remote

### Remote BOM

| Qty | Component | Specification |
|---|---|---|
| 1 | ESP-01 or ESP-01S | ESP8266, 512 KB flash |
| 1 | Tactile push button | Momentary — wired in series with VCC |
| 1 | Hold-up capacitor | **1000 µF – 2200 µF** electrolytic (≥ 6.3 V) **OR** 0.1 F / 5.5 V supercap + 22 Ω series resistor |
| 3 | 10 kΩ resistors | Pull-ups for CH_PD, RST, GPIO0 |
| 1 | 330 Ω resistor | LED current limiter |
| 1 | LED (3 mm / 5 mm) | Boot status indicator |
| 1 | Battery | 2× AA (3.0 V) / CR123A (3.0 V) / LiPo (3.7 V) |

### ESP-01 Pin Reference

| Pin # | Name | Connect To | Why |
|---|---|---|---|
| 1 | GND | Battery (−), Cap (−) | Ground |
| 2 | TXD | *Nothing* | Not used |
| 3 | GPIO2 | 330 Ω → LED cathode | Status LED (active LOW) |
| 4 | CH_PD | 10 kΩ → VCC | **Must be HIGH to boot** |
| 5 | GPIO0 | 10 kΩ → VCC | **HIGH = Run mode** (LOW = flash mode) |
| 6 | RST | 10 kΩ → VCC | **Must be HIGH** (prevents reboot loop) |
| 7 | RXD | *Nothing* | Not used |
| 8 | VCC | Button → Battery (+), Cap (+) | Main power rail (2.8 – 3.6 V) |

### Remote Assembly Photo

> 📷 *[Insert photo: `docs/remote_assembly.jpg`]*
>
> *ESP-01 with pull-ups, hold-up capacitor, and VCC button.*

### ⚠️ Critical: The Hold-Up Capacitor

The capacitor is **not optional**. It keeps the ESP-01 alive for ~100–200 ms after you release the button, giving the EEPROM write time to finish.

```
Without cap:  Button release → instant power loss → EEPROM write may fail → counter desync
With cap:     Button release → cap drains slowly → EEPROM write completes → safe shutdown
```

> 📷 *[Insert photo: `docs/capacitor_detail.jpg`]*
>
> *Close-up: 1000 µF electrolytic across VCC and GND on the remote.*

---

## 5. Wiring Diagrams

### Remote Schematic

```
                        [ BATTERY (+) ]
                              │
                       [ PUSH BUTTON ]        ← VCC switch
                              │
     ┌────────────────────────┼────────────────────────┐
     │                        │                        │
  [ CAP ]               [10kΩ] × 3               [LED +]
  1000µF              (CH_PD, RST, GPIO0)           │
  (+ to VCC)                │                   [LED −]
  (− to GND)                │                        │
     │                      │                     [330Ω]
     │    ┌─────────────────┼─────────────────┐      │
     │    │                 │                 │      │
     └────┤ Pin 8 (VCC)    Pin 4 (CH_PD)   Pin 3 (GPIO2)
          │                 │                 │      │
          │    ┌────────────┴─────────────────┘      │
          │    │         ESP-01 / 01S                │
          │    │                                     │
          └────┤ Pin 1 (GND)   Pin 5 (GPIO0)   Pin 6 (RST)
               │                 │                 │
               │              [10kΩ]            [10kΩ]
               │                 │                 │
               └─────────────────┴─────────────────┘
                                 │
                          [ BATTERY (−) ]
```

> 📷 *[Insert image: `docs/remote_schematic.png`]*
>
> *Clean schematic version of the above.*

### Receiver Schematic (NodeMCU + Relay)

```
     NodeMCU v3
  ┌──────────────┐
  │  3V3 ────────┼──── (not used)
  │  GND ────────┼────┬──── Relay GND
  │              │    │     Button pin 2
  │  D5 (GPIO14)─┼────┼──── Relay SIGNAL
  │  D2 (GPIO4)──┼────┘     Button pin 1 → GND
  │              │
  │  VIN ────────┼──── USB 5V (always-on power)
  └──────────────┘
```

> 📷 *[Insert image: `docs/receiver_schematic.png`]*

---

## 6. Software Setup & Flashing

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB-to-serial adapter (for ESP-01) or micro-USB cable (NodeMCU)

### Step 1 — Clone / Download the Project

```bash
git clone <your-repo-url>
cd RemoteDoorBell
```

### Step 2 — Flash the RECEIVER First

```bash
# For NodeMCU / ESP-12F:
pio run -e receiver_12e -t upload

# For bare ESP-01 (512 KB):
pio run -e receiver_01 -t upload
```

After flashing, **open the serial monitor** (74880 baud) and note the receiver's MAC address:

```
=============================
 Doorbell Receiver v1.0 (Fire-and-Forget)
=============================
  MAC Addr  : 68:C6:3A:D6:59:48    ← COPY THIS
  Chip ID   : 0x1A2B3C4D
```

### Step 3 — Update the Remote Firmware

Open `src/remote_main.cpp` and paste the receiver's MAC:

```cpp
uint8_t receiverMac[] = {0x68, 0xC6, 0x3A, 0xD6, 0x59, 0x48};
//                         ^^    ^^    ^^    ^^    ^^    ^^
//                         Paste your receiver's MAC here
```

### Step 4 — Flash the REMOTE

```bash
pio run -e remote_01 -t upload
```

> ⚠️ **ESP-01 flashing tip:** GPIO0 must be **LOW** during flash, then **HIGH** to run.
> Use a USB-to-serial adapter with a flash/run switch, or manually jumper GPIO0 → GND during upload.

### Step 5 — Set Production Mode

In **both** `remote_main.cpp` and `receiver_main.cpp`:

```cpp
#define PLAINTEXT_DEBUG 0   // ← MUST be 0 for real deployment
```

> ⚠️ Both files **must** have the same value. Mismatch = receiver can't decrypt packets.

---

## 7. Configuration Reference

### Receiver (`receiver_main.cpp`)

| Define | Default | Options | Description |
|---|---|---|---|
| `OUTPUT_MODE` | `OUTPUT_MODE_RELAY` | `1` = Relay, `2` = Passive buzzer (ding-dong), `3` = Active buzzer | What happens on ring |
| `OUTPUT_PIN` | `D5` | Any GPIO | Pin driving the output device |
| `PAIRING_BTN_PIN` | `D2` | Any GPIO | Pairing button (active LOW) |
| `RING_DURATION` | `700` | ms | How long the relay stays on |
| `MAX_REMOTES` | `8` | 1–8 | Max paired remotes |
| `PAIRING_WINDOW` | `10000` | ms | Pairing mode timeout |
| `RELAY_ACTIVE_HIGH` | `0` | `0` or `1` | Relay trigger polarity |
| `CHANNEL` | `2` | 1–13 | ESP-NOW WiFi channel |
| `PLAINTEXT_DEBUG` | `1` | `0` = encrypted, `1` = plaintext | **Must match remote** |

### Remote (`remote_main.cpp`)

| Define | Default | Description |
|---|---|---|
| `receiverMac[]` | *(must edit)* | Receiver's MAC address |
| `CHANNEL` | `2` | Must match receiver |
| `TX_RETRIES` | `3` | Number of packet blasts |
| `PLAINTEXT_DEBUG` | `1` | Must match receiver |
| `LED_PIN` | `2` (GPIO2) | Boot status LED |

### AES Key (both files)

```cpp
static const uint8_t AES_KEY[AES_KEYLEN] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};
```

> 🔑 **Change this key for your own deployment!** Both files must use the identical key.

---

## 8. Pairing a Remote

The receiver must "learn" each remote before it will respond to it.

### Steps

1. **Power on the receiver.** Wait for `[READY] Listening for remotes` in serial.
2. **Press the pairing button** (D2 → GND) once.
   - Relay/buzzer blinks 3 times → pairing mode is **OPEN** (10-second window).
3. **Press the remote's doorbell button** within 10 seconds.
4. Serial monitor shows:
   ```
   [PAIR] New remote registered | Slot 0 | ID: 0x1A2B3C4D
   ```
5. Pairing mode auto-closes after 10 s (or press the button again to close early).

### Unpairing / Factory Reset

> Hold the pairing button for **10 seconds** to wipe all paired remotes from EEPROM.
> *(Feature recommended in review — implement if not yet present.)*

### Multi-Remote

Repeat the pairing steps for each additional remote (up to 8 slots). Each remote is identified by its unique ESP8266 Chip ID.

---

## 9. Security & Encryption

### Packet Structure (16 bytes)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 1 byte | `type` | `1` = RING command |
| 1 | 4 bytes | `sender` | ESP8266 Chip ID |
| 5 | 4 bytes | `counter` | Rolling counter (increments every press) |
| 9 | 7 bytes | `pad` | Zero-padding to fill AES block |

### Protection Layers

| Layer | Mechanism |
|---|---|
| **Confidentiality** | AES-128 ECB encryption (disabled when `PLAINTEXT_DEBUG=1`) |
| **Replay protection** | Rolling counter — receiver rejects `delta == 0` (duplicate) or `delta > 0xF0000000` (rollback) |
| **Authentication** | Sender Chip ID must be in the EEPROM whitelist |
| **Pairing gate** | Unknown senders are dropped unless pairing mode is active |

### ⚠️ Production Checklist

- [ ] `PLAINTEXT_DEBUG` set to `0` in **both** files
- [ ] AES key changed from the default
- [ ] Receiver serial debug can be left on (doesn't leak keys)

---

## 10. Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| Receiver reboots randomly (`Exception 9`) | Memory alignment crash | Already fixed — ensure you're using `memcpy` in `onReceive()`, not a pointer cast |
| Remote presses are ignored (serial shows `delta=0`) | Counter desync — EEPROM write failed | Add/increase the hold-up capacitor (≥ 1000 µF). Verify `delay(100)` after `EEPROM.commit()` |
| No output at all | `PLAINTEXT_DEBUG` mismatch | Both files must be `0` or both `1` |
| Relay clicks on boot | GPIO glitch during ESP8266 startup | Already fixed — `digitalWrite(OFF)` is called before `pinMode(OUTPUT)` |
| Pairing doesn't work | Remote MAC not added as peer | Ensure receiver calls `addRemotePeer()` after registration |
| ESP-01 won't boot | CH_PD or RST floating | Both need 10 kΩ pull-ups to VCC |
| ESP-01 stuck in flash mode | GPIO0 is LOW at boot | GPIO0 must be pulled HIGH (10 kΩ) for normal run |
| Range is poor | Low TX power or wrong channel | `WiFi.setOutputPower(20.5)` is already max. Verify `CHANNEL` matches on both sides |

---

## 11. Known Fixes & Engineering Notes

### Fix 1 — Exception 9 (Memory Alignment)

**Problem:** Casting `uint8_t*` → `Packet*` on Xtensa violates 4-byte alignment.

**Solution:** Use `memcpy` into a stack-allocated struct:

```cpp
// ❌ BAD — causes Exception 9
Packet *pkt = (Packet *)plain;

// ✅ GOOD — safe aligned copy
Packet pkt;
memcpy(&pkt, plain, sizeof(pkt));
```

### Fix 2 — Power-Button Race Condition

**Problem:** Releasing the VCC button too fast kills power before `EEPROM.commit()` finishes → counter never saves → next press sends the same counter → receiver rejects it as duplicate forever.

**Solution:** Increment and save the counter **immediately** on boot, before WiFi init or TX:

```cpp
void setup() {
    // ... GPIO, AES init ...
    loadCounter();
    messageCounter++;
    saveCounter(messageCounter);  // ← EEPROM write happens HERE, first
    delay(100);                   // ← wait for flash to settle
    // ... NOW do WiFi, build packet, transmit ...
}
```

### Fix 3 — Boot-Time Relay Glitch

**Problem:** ESP8266 GPIOs float LOW during early boot → active-LOW relay clicks on briefly.

**Solution:** Pre-load the output latch before enabling the pin driver:

```cpp
digitalWrite(OUTPUT_PIN, RELAY_OFF);  // ← set safe level FIRST
pinMode(OUTPUT_PIN, OUTPUT);          // ← then enable the driver
```

---

## 📁 Project File Map

```
RemoteDoorBell/b 
├── src/
│   ├── receiver_main.cpp      ← Receiver firmware
│   ├── remote_main.cpp        ← Remote firmware
│   └── aes/                   ← AES-128 library (tiny-AES-c)
├── docs/
│   ├── receiver_wiring.jpg    ← 📷 Receiver photo
│   ├── remote_assembly.jpg    ← 📷 Remote photo
│   ├── capacitor_detail.jpg   ← 📷 Capacitor close-up
│   ├── remote_schematic.png   ← 📷 Remote schematic
│   └── receiver_schematic.png ← 📷 Receiver schematic
├── platformio.ini             ← Build environments
├── README.md                  ← Quick-start overview
├── USER_REVIEW.md             ← Engineering changelog
└── HardwareConfigRemote_Side.md ← Remote hardware deep-dive
```

---

## 🔗 Video Demo

> 🎬 **[Watch the full build + setup guide on YouTube →](#)**
>
> *Timestamps:*
> - `0:00` — Overview & demo
> - `2:15` — Receiver wiring
> - `5:30` — Remote assembly (ESP-01 + capacitor)
> - `9:00` — Flashing with PlatformIO
> - `12:45` — Pairing & testing
> - `15:00` — Production hardening tips

---

*Last updated: August 2026 · Firmware v1.0 · Fire-and-Forget Architecture*