# Doorbell ESP-NOW — Review, Architecture & Recommendations

## Logical Integrity Review
The project architecture is highly coherent, optimized for a "Fire-and-Forget" paradigm that eliminates handshake latency. The receiver handles packet validation, duplicate suppression, and output triggering. The remote handles boot-triggered transmission, early counter persistence, and immediate power-down.

## Current Logic Findings
* **Receiver packet flow is correct & safe**: Decrypt → `memcpy` to aligned struct → validate → find/learn remote → replay suppression → trigger output.
* **Remote transmission logic is correct**: Boot → Increment & Save Counter → Build Packet → Blast 3x → Sleep/Power-off.
* **Rolling counter protection** is robust. The `delta` calculation correctly handles 32-bit unsigned integer wrap-arounds and blocks malicious rollbacks.
* **EEPROM storage** for paired remotes and counter state is adequate and optimized to reduce flash wear.

## Recent Critical Bug Fixes Implemented

### 1. ESP8266 Exception 9 (Memory Alignment Crash)
* **Issue**: The receiver previously cast a `uint8_t*` byte array directly to a `Packet*` struct pointer. Because the struct contains 32-bit integers, this violated the Xtensa architecture's 4-byte memory alignment requirement, causing random `Exception 9` (LoadStoreAlignmentCause) reboots.
* **Fix**: Replaced the pointer cast with `memcpy(&pkt, plain, sizeof(pkt));` in `onReceive()` to ensure the data is safely copied into a properly aligned stack variable.

### 2. Power-Button Race Condition (Counter Desync)
* **Issue**: When the remote was powered via a VCC button, releasing the button too quickly cut power before `EEPROM.commit()` could finish. The remote would fail to save the *next* counter, causing it to send the *old* counter on the next press. The receiver would reject this as a duplicate (`delta=0`).
* **Fix**: Implemented the **"Increment Early"** strategy. The remote now increments and saves the counter *immediately* upon boot (before WiFi init or TX). Added a `delay(50)` after `EEPROM.commit()` to guarantee the flash write completes.

### 3. Boot-Time Relay Glitch
* **Issue**: Standard ESP8266 boot sequences can cause momentary LOW pulses on GPIO pins, accidentally triggering active-LOW relays.
* **Fix**: The receiver `setup()` explicitly calls `digitalWrite(OUTPUT_PIN, RELAY_OFF)` *before* calling `pinMode(OUTPUT_PIN, OUTPUT)`. This pre-loads the output latch with the safe idle level before the push-pull driver is enabled.

## Hardware / Software Suggestions

### Hardware Recommendations
* **Remote Capacitor**: A 100µF - 470µF electrolytic capacitor across the Remote's VCC and GND is mandatory for reliable VCC-switching. It provides the necessary hold-up time for the EEPROM write if the user "taps" the button.
* Use a proper 3.3V regulator (e.g., HT7333 or MCP1700) if running from a LiPo battery.
* Ensure the pairing button on the receiver uses active-LOW wiring with a strong 10K pull-up.

### Software Recommendations
* Set `PLAINTEXT_DEBUG` to `0` in both files for production deployment.
* Consider making output triggering non-blocking when `OUTPUT_MODE` is `BUZZER_TONE` to prevent Soft WDT resets during the `tone()` delays.
* Add a "Clear All Remotes" hardware sequence (e.g., holding the pairing button for 10 seconds) to wipe the EEPROM whitelist without needing a serial connection.

## Recommended Docs
* `README.md` provides a high-level overview, quick-start guide, and packet flow.
* `docs/ARCHITECTURE.md` describes the system roles, Fire-and-Forget paradigm, wiring, and memory alignment fixes.
* `docs/USER_REVIEW.md` (this file) serves as the engineering changelog and troubleshooting reference.

## Build Verification
The PlatformIO build settings (`platformio.ini`) and binary paths are valid for the supported environments (NodeMCU v2/v3, D1 Mini, and bare ESP-01). Flash layouts correctly utilize `eagle.flash.512k64.ld` for the 512KB ESP-01 constraints.