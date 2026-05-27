# Doorbell ESP-NOW — User Review & Build Notes

## What was fixed

The project had unresolved merge-conflict markers in the firmware sources, which prevented PlatformIO from compiling the ESP8266 targets.

The following repairs were applied:

- Removed broken conflict markers from the receiver and remote firmware sources.
- Restored the intended receiver output-mode logic (relay / buzzer modes).
- Fixed the ESP-01 receiver pin configuration so the bare ESP-01 build uses GPIO2 and GPIO0 instead of NodeMCU aliases.
- Added environment-specific build flags in PlatformIO for the ESP-01 receiver target.

## Verified build status

The project was verified with these commands:

- `platformio run -e receiver_12e` → SUCCESS
- `platformio run -e receiver_01` → SUCCESS
- `platformio run -e remote` → SUCCESS

These checks confirm the repaired firmware now compiles for all supported targets.

## Hardware setup notes

### Receiver (recommended: NodeMCU / ESP-12E)

- Use environment: `receiver_12e`
- Output pin defaults to `D5`.
- Pairing button defaults to `D2`.

### Receiver (fallback: bare ESP-01)

- Use environment: `receiver_01`
- The PlatformIO configuration now automatically maps:
  - `OUTPUT_PIN = 2` (GPIO2)
  - `PAIRING_BTN_PIN = 0` (GPIO0)
- Note: GPIO0 is sensitive during boot; keep it pulled up or floating unless entering flash mode.

### Remote transmitter (ESP-01)

- Use environment: `remote`
- The remote uses the receiver MAC address stored in `receiverMac[]`.
- Update that array with the receiver’s actual MAC before field deployment.

## Production / debug mode

- `PLAINTEXT_DEBUG 0` → production mode with AES enabled
- `PLAINTEXT_DEBUG 1` → development mode with raw packets (use only for testing)

Keep this value the same on both the remote and receiver firmware.

## Recommended next steps

1. Flash the receiver first and confirm the MAC address on the serial monitor.
2. Copy that MAC into the remote firmware `receiverMac[]` array.
3. Flash the remote and test one press cycle.
4. If the relay or buzzer wiring is different, adjust `OUTPUT_MODE` in `receiver_main.cpp` to match your hardware.
