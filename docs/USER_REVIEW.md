# Doorbell ESP-NOW — Review, Architecture & Recommendations

## Logical integrity review

The project architecture is coherent and the firmware roles are clearly separated.
The receiver handles pairing, packet validation, duplicate suppression, output triggering, and ACK admission. The remote handles button-triggered transmission, ACK monitoring, counter persistence, and deep sleep.

### Current logic findings

- **Receiver packet flow** is correct: decrypt → validate → find/learn remote → replay suppression → trigger output → send ACK.
- **Remote transmission logic** is correct: build packet → send with retries → wait for ACK → save counter → deep sleep.
- **Rolling counter protection** is implemented and prevents exact duplicate or rollback-like packets.
- **EEPROM storage** for paired remotes and counter state is adequate for the intended use.

### Important improvements made

- Added explicit relay polarity configuration in `src/receiver_main.cpp` with `RELAY_ACTIVE_HIGH`.
- Improved pairing button debounce in `src/receiver_main.cpp` so mode toggles only on button press transitions.
- Added documentation clarity around the receiver/remote roles and configuration points.

## Hardware / software suggestions

### Hardware recommendations

- Use a proper 3.3V regulator and decoupling capacitors on both ESP8266 boards.
- For the relay output, use a driver transistor or optocoupler if the relay module does not tolerate direct GPIO switching.
- Ensure the receiver output pin is matched to the board type:
  - NodeMCU/ESP-12E: `D5`
  - ESP-01 receiver: `GPIO2`
- Keep the pairing button on the receiver using active-LOW wiring with a strong pull-up.
- Add a small indicator LED or buzzer on the receiver for pairing and fault states.

### Software recommendations

- Set `PLAINTEXT_DEBUG` to `0` in both `src/receiver_main.cpp` and `src/remote_main.cpp` for production.
- Consider adding a remote removal / reset mechanism on the receiver.
- Consider making output triggering non-blocking when `OUTPUT_MODE` is `BUZZER_TONE` for better responsiveness.
- Add consistency checks for `esp_now_add_peer()` result values and log failure cases.
- Replace hardcoded `receiverMac[]` assignment with a simple build-time or config-time value when possible.
- Consider a stronger message authentication layer beyond AES-ECB if the project evolves.

## Recommended docs

- `README.md` now includes a high-level overview and the packet flow.
- `docs/ARCHITECTURE.md` describes the system roles, wiring, and improvement suggestions.
- Keep `docs/USER_REVIEW.md` as a quick troubleshooting and validation reference.

## Build verification

The build settings and binary paths are valid for the supported environments. The next step is field validation with the intended hardware and actual receiver MAC values.
