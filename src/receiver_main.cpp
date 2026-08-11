// ============================================================
//  DOORBELL RECEIVER FIRMWARE — NodeMCU v3 / Lolin (ESP-12F)
//  Features: Multi-remote pairing/unpairing, AES-128 ECB encryption,
//            EEPROM whitelist persistence, duplicate suppression,
//            rollback auto-block (10 s) with auto-re-sync,
//            boot-safe relay init, Serial debug output
//
// ============================================================
//  ESP32 PORTING GUIDE
//  platformio.ini:  platform = espressif32 / board = esp32dev
//  Key API changes needed in source:
//   1. Includes:
//        <ESP8266WiFi.h>           → <WiFi.h>
//        extern "C" { <espnow.h> } → <esp_now.h>  (already C-compat)
//        <EEPROM.h>                → <Preferences.h>
//   2. Channel set:
//        wifi_set_channel(ch)      → esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE)
//   3. TX power:
//        WiFi.setOutputPower(20.5) → esp_wifi_set_max_tx_power(84) // 84 = 21 dBm
//   4. Chip ID:
//        ESP.getChipId()           → (uint32_t)ESP.getEfuseMac()
//   5. ESP-NOW add_peer:
//        esp_now_add_peer(mac, role, ch, NULL, 0)
//                                  → esp_now_peer_info_t p={}; memcpy(p.peer_addr,mac,6);
//                                    p.channel=ch; p.encrypt=false; esp_now_add_peer(&p);
//   6. Recv callback signature:
//        void cb(uint8_t *mac, uint8_t *data, uint8_t len)
//                                  → void cb(const esp_now_recv_info_t *info,
//                                            const uint8_t *data, int len)
//                                    mac = info->src_addr
//   7. tone() / noTone():
//        tone(pin, freq, ms)       → ledcAttach(pin,freq,10); ledcWrite(pin,512);
//        noTone(pin)               → ledcWrite(pin,0);
//   8. EEPROM:
//        EEPROM.begin/get/put/write/commit
//                                  → Preferences prefs;
//                                    prefs.begin("doorbell"); prefs.putBytes(...);
//  EEPROM replacement is the largest change; consider keeping <EEPROM.h>
//  which also works on ESP32 Arduino core (same API, different backend).
// ============================================================

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
extern "C" {
  #include <espnow.h>
  #include "ase/aes.h"     // tiny-AES-c — local, no SDK dependency
}

// ---- Debug output -----------------------------------------
// DEBUG_SERIAL 1 = full serial logging (development)
// DEBUG_SERIAL 0 = no serial code compiled (production)
#define DEBUG_SERIAL 1

#if DEBUG_SERIAL
  #define DBG_BEGIN(baud) Serial.begin(baud)
  #define DBG(msg)        Serial.print(msg)
  #define DBGLN(msg)      Serial.println(msg)
  #define DBGF(...)       Serial.printf(__VA_ARGS__)
#else
  #define DBG_BEGIN(baud)
  #define DBG(msg)
  #define DBGLN(msg)
  #define DBGF(...)
#endif

// ---- Plaintext debug mode --------------------------------
// PLAINTEXT_DEBUG 1 = skip AES entirely, expect raw packets.
// MUST match the same value in remote_main.cpp.
// MUST be 0 for production deployment.
#define PLAINTEXT_DEBUG 1

// ---- Output mode ------------------------------------------
// Choose ONE output mode. Comment out the others.
//
// RELAY         — Active-LOW relay module (standard SRD-05VDC / HY-SRD wiring).
//                 Pin LOW  = relay coil energized  = bell rings.
//                 Pin HIGH = relay coil released   = idle (safe on boot).
//                 The relay module's own pull-up holds IN HIGH during the ESP
//                 boot window before our code runs — relay stays off.
//                 Duration: RING_DURATION ms then releases automatically.
//
// BUZZER_TONE   — Passive piezo buzzer with ding-dong rhythm pattern.
//                 Plays a two-tone doorbell sequence using tone().
//                 Requires a passive buzzer (not self-oscillating active buzzer).
//
// BUZZER_SIMPLE — Active buzzer or passive with simple on/off.
//                 Pin HIGH = buzzer on. Duration: RING_DURATION ms.
//
#define OUTPUT_MODE_RELAY         1
#define OUTPUT_MODE_BUZZER_TONE   2
#define OUTPUT_MODE_BUZZER_SIMPLE 3

#define OUTPUT_MODE  OUTPUT_MODE_RELAY   // ← change this line to switch mode

// ---- Configuration ----------------------------------------
#define CHANNEL          1
#ifndef OUTPUT_PIN
#define OUTPUT_PIN       D5        // Relay signal pin or buzzer pin
#endif
                                   // ESP-01 receiver: change to 2 (GPIO2)
#ifndef PAIRING_BTN_PIN
#define PAIRING_BTN_PIN  D2        // Active LOW — INPUT_PULLUP, button to GND
#endif
                                   // ESP-01 receiver: change to 0 (GPIO0, careful)
#define RING_DURATION    700       // ms relay held / buzzer on
#define MAX_REMOTES      8
#define PAIRING_WINDOW   10000     // ms pairing mode stays open
#define ROLLBACK_BLOCK_MS 10000   // ms a remote is suppressed after a counter rollback
                                   // After this window, next press auto-re-syncs counter

// Relay polarity — match to your relay module:
//   0 = Active-LOW  (default — standard SRD-05VDC, HY-SRD, most cheap modules)
//         LOW  = coil energized (bell rings)
//         HIGH = coil released  (idle, safe boot state)
//   1 = Active-HIGH (uncommon — use only if your module fires on HIGH)
//         HIGH = coil energized (bell rings)
//         LOW  = coil released  (idle)
//
// Active-LOW modules have an onboard optocoupler and pull-up on IN.
// That pull-up holds the relay OFF during the ESP boot window before
// our GPIO driver is enabled — no accidental ring on power-up.
#define RELAY_ACTIVE_HIGH 0
#if RELAY_ACTIVE_HIGH
  #define RELAY_ON   HIGH
  #define RELAY_OFF  LOW
#else
  #define RELAY_ON   LOW
  #define RELAY_OFF  HIGH
#endif

// Buzzer tone frequencies for ding-dong pattern (passive buzzer only)
#define NOTE_DING  1047   // C6
#define NOTE_DONG   784   // G5
#define TONE_MS     300   // ms per note

// AES-128 shared key — 16 bytes, MUST match remote exactly
// Replace with your own random bytes before deployment
static const uint8_t AES_KEY[AES_KEYLEN] = {
  0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
  0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};

// ---- EEPROM layout ----------------------------------------
// 20 bytes per remote slot:
//   [0]     active flag (uint8_t)
//   [1..4]  senderID    (uint32_t)
//   [5..8]  lastCounter (uint32_t)
//   [9..14] mac         (uint8_t[6])
//   [15..19] reserved   (padding)
#define EEPROM_SLOT_SIZE  20
#define EEPROM_MAGIC_ADDR (MAX_REMOTES * EEPROM_SLOT_SIZE)  // 160
#define EEPROM_MAGIC_VAL  0xCAFEBABEu
#define EEPROM_TOTAL      (EEPROM_MAGIC_ADDR + 4)            // 164

// ---- Packet structures ------------------------------------
typedef struct {
  uint8_t  type;      // 1 = RING, 2 = ACK
  uint32_t sender;    // chip ID
  uint32_t counter;   // rolling counter
  uint8_t  pad[7];    // padding to exactly 16 bytes
} __attribute__((packed)) Packet;

typedef struct {
  uint8_t data[AES_BLOCKLEN];
} WirePacket;

// Compile-time guard — fails build if Packet size drifts from 16 bytes
typedef char _packet_size_check[(sizeof(Packet) == AES_BLOCKLEN) ? 1 : -1];

// ---- Remote state record ----------------------------------
struct RemoteRecord {
  bool          active;
  uint32_t      senderID;
  uint32_t      lastCounter;
  uint8_t       mac[6];

  // RAM-only — not stored in EEPROM, resets on every power cycle.
  // 0           = not blocked.
  // >0          = millis() timestamp at which the block expires.
  // Set when a counter rollback is detected (remote re-flashed and
  // EEPROM counter reset).  Clears automatically when the window
  // expires.  Allows a remotes whose counter rolled back to ring
  // again after 10 s without needing manual pairing-mode re-sync.
  unsigned long blockedUntil;
};

// ---- Globals ----------------------------------------------
RemoteRecord   remotes[MAX_REMOTES];
struct AES_ctx aesCtx;

bool          ringing      = false;
unsigned long ringStart    = 0;
bool          pairingMode  = false;
unsigned long pairingStart = 0;

// ---- Forward declarations ---------------------------------
void saveRemote(int idx);
void saveCounter(int idx);
void loadAllRemotes();
void addRemotePeer(uint8_t *mac);          // add/refresh ESP-NOW peer entry
int  findRemote(uint32_t senderID);
int  registerRemote(uint32_t senderID, uint8_t *mac);
void unpairRemote(int idx);                // remove known remote from whitelist
void sendAck(uint8_t *mac, uint32_t originalSenderID);
void onAckSent(uint8_t *mac, uint8_t status);  // send-callback for debug
void enterPairingMode();
void exitPairingMode();
void onReceive(uint8_t *mac, uint8_t *data, uint8_t len);

// ---- EEPROM persistence -----------------------------------
void saveRemote(int idx) {
  int base = idx * EEPROM_SLOT_SIZE;
  EEPROM.write(base,     remotes[idx].active ? 1 : 0);
  EEPROM.put(base + 1,   remotes[idx].senderID);
  EEPROM.put(base + 5,   remotes[idx].lastCounter);
  for (int b = 0; b < 6; b++) {
    EEPROM.write(base + 9 + b, remotes[idx].mac[b]);
  }
  EEPROM.commit();
}

void saveCounter(int idx) {
  // Fast path — only write the 4-byte counter to reduce flash wear
  int base = idx * EEPROM_SLOT_SIZE;
  EEPROM.put(base + 5, remotes[idx].lastCounter);
  EEPROM.commit();
}

void loadAllRemotes() {
  EEPROM.begin(EEPROM_TOTAL);
  uint32_t magic = 0;
  EEPROM.get(EEPROM_MAGIC_ADDR, magic);

  if (magic != EEPROM_MAGIC_VAL) {
    DBGLN("[EEPROM] First boot — initialising empty remote table");
    for (int i = 0; i < MAX_REMOTES; i++) {
      memset(&remotes[i], 0, sizeof(RemoteRecord));
    }
    EEPROM.put(EEPROM_MAGIC_ADDR, (uint32_t)EEPROM_MAGIC_VAL);
    EEPROM.commit();
    return;
  }

  int activeCount = 0;
  for (int i = 0; i < MAX_REMOTES; i++) {
    int base = i * EEPROM_SLOT_SIZE;
    remotes[i].active = (EEPROM.read(base) == 1);
    EEPROM.get(base + 1, remotes[i].senderID);
    EEPROM.get(base + 5, remotes[i].lastCounter);
    for (int b = 0; b < 6; b++) {
      remotes[i].mac[b] = EEPROM.read(base + 9 + b);
    }
    if (remotes[i].active) {
      activeCount++;
      DBGF("[EEPROM] Slot %d | ID: 0x%08X | Counter: %u | MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        i, remotes[i].senderID, remotes[i].lastCounter,
        remotes[i].mac[0], remotes[i].mac[1], remotes[i].mac[2],
        remotes[i].mac[3], remotes[i].mac[4], remotes[i].mac[5]);
    }
  }
  DBGF("[EEPROM] %d/%d remote slots active\n", activeCount, MAX_REMOTES);
}

// ---- Peer management --------------------------------------
// ESP8266 ESP-NOW requires a peer entry before esp_now_send()
// will unicast to that MAC. Without this, send calls silently fail.
// We call this at boot (for all known remotes) and on registration.
// Idempotent: deletes stale entry first so re-adding always works.
void addRemotePeer(uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) {
    esp_now_del_peer(mac);   // remove stale entry before re-adding
  }
  // COMBO peer role — receiver needs to SEND ACKs back to the remote,
  // not just receive from it. CONTROLLER peer role blocks outbound sends.
  int result = esp_now_add_peer(mac, ESP_NOW_ROLE_COMBO, CHANNEL, NULL, 0);
  DBGF("[PEER] add %02X:%02X:%02X:%02X:%02X:%02X → %s\n",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
    (result == 0) ? "OK" : "FAILED");
}

// ---- Remote table -----------------------------------------
int findRemote(uint32_t senderID) {
  for (int i = 0; i < MAX_REMOTES; i++) {
    if (remotes[i].active && remotes[i].senderID == senderID) return i;
  }
  return -1;
}

int registerRemote(uint32_t senderID, uint8_t *mac) {
  for (int i = 0; i < MAX_REMOTES; i++) {
    if (!remotes[i].active) {
      remotes[i].active       = true;
      remotes[i].senderID     = senderID;
      remotes[i].lastCounter  = 0;
      remotes[i].blockedUntil = 0;   // clear any lingering block on slot reuse
      memcpy(remotes[i].mac, mac, 6);
      saveRemote(i);
      addRemotePeer(mac);   // CRITICAL: must add peer before we can ACK this remote
      DBGF("[PAIR] New remote registered | Slot %d | ID: 0x%08X\n", i, senderID);
      return i;
    }
  }
  DBGLN("[PAIR] ERROR: Remote table full");
  return -1;
}

// ---- Unpair -----------------------------------------------
// Removes a known remote from the whitelist.
// Called when a already-paired remote is pressed during pairing mode.
// Clears the EEPROM slot, removes the ESP-NOW peer, then gives
// 5 rapid output pulses as user feedback (distinct from pairing sounds).
void unpairRemote(int idx) {
  DBGF("[PAIR] UNPAIRING remote 0x%08X | slot %d | MAC: "
       "%02X:%02X:%02X:%02X:%02X:%02X\n",
    remotes[idx].senderID, idx,
    remotes[idx].mac[0], remotes[idx].mac[1], remotes[idx].mac[2],
    remotes[idx].mac[3], remotes[idx].mac[4], remotes[idx].mac[5]);

  if (esp_now_is_peer_exist(remotes[idx].mac)) {
    esp_now_del_peer(remotes[idx].mac);
    DBGLN("[PAIR] ESP-NOW peer removed");
  }

  // Zero the entire record — sets active=0, clears MAC, counter, blockedUntil
  memset(&remotes[idx], 0, sizeof(RemoteRecord));
  saveRemote(idx);   // persists active=0 to EEPROM, slot is now free
  DBGF("[PAIR] Slot %d cleared — remote is unpaired\n", idx);

  // 5 rapid pulses = unpaired feedback (distinct from 3-pulse pair-open,
  // 1-long-pulse pair-close, and 2-pulse boot-ready signals)
#if OUTPUT_MODE == OUTPUT_MODE_RELAY
  for (int i = 0; i < 5; i++) {
    digitalWrite(OUTPUT_PIN, RELAY_ON);  delay(50);
    digitalWrite(OUTPUT_PIN, RELAY_OFF); delay(50);
  }
#elif OUTPUT_MODE == OUTPUT_MODE_BUZZER_TONE
  for (int i = 0; i < 5; i++) {
    tone(OUTPUT_PIN, 3000, 50);
    delay(110);
  }
#else
  for (int i = 0; i < 5; i++) {
    digitalWrite(OUTPUT_PIN, HIGH); delay(50);
    digitalWrite(OUTPUT_PIN, LOW);  delay(50);
  }
#endif
}

// ---- Send callback (debug) --------------------------------
// Tells us whether esp_now_send actually delivered at PHY level.
// Useful during development to confirm ACK packets are leaving.
void onAckSent(uint8_t *mac, uint8_t status) {
  if (status != 0) {
    DBGF("[ACK] PHY send FAILED to %02X:%02X:%02X:%02X:%02X:%02X (status=%d)\n",
      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], status);
  }
}

// ---- ACK --------------------------------------------------
void sendAck(uint8_t *mac, uint32_t originalSenderID) {
  Packet pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.type    = 2;
  pkt.sender  = originalSenderID;  // echoed — remote verifies this matches its chip ID
  pkt.counter = 0;

  WirePacket wire;
  memcpy(wire.data, &pkt, AES_BLOCKLEN);
#if PLAINTEXT_DEBUG == 0
  AES_ECB_encrypt(&aesCtx, wire.data);
#endif

  int rc = esp_now_send(mac, wire.data, sizeof(wire));
  DBGF("[ACK] esp_now_send to %02X:%02X:%02X:%02X:%02X:%02X → %s\n",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
    (rc == 0) ? "queued OK" : "REJECTED by SDK");
}

// ---- Pairing mode -----------------------------------------
void enterPairingMode() {
  pairingMode  = true;
  pairingStart = millis();
  DBGLN("[PAIR] Pairing mode OPEN — press remote button within 10s");

  // 3 rapid beeps/clicks = pairing open
#if OUTPUT_MODE == OUTPUT_MODE_RELAY
  for (int i = 0; i < 3; i++) {
    digitalWrite(OUTPUT_PIN, RELAY_ON);  delay(80);
    digitalWrite(OUTPUT_PIN, RELAY_OFF); delay(80);
  }
#elif OUTPUT_MODE == OUTPUT_MODE_BUZZER_TONE
  for (int i = 0; i < 3; i++) {
    tone(OUTPUT_PIN, 2000, 80);
    delay(160);
  }
#else
  for (int i = 0; i < 3; i++) {
    digitalWrite(OUTPUT_PIN, HIGH); delay(80);
    digitalWrite(OUTPUT_PIN, LOW);  delay(80);
  }
#endif
}

void exitPairingMode() {
  pairingMode = false;
  DBGLN("[PAIR] Pairing mode CLOSED");

  // 1 long click/beep = closed
#if OUTPUT_MODE == OUTPUT_MODE_RELAY
  digitalWrite(OUTPUT_PIN, RELAY_ON);  delay(300);
  digitalWrite(OUTPUT_PIN, RELAY_OFF);
#elif OUTPUT_MODE == OUTPUT_MODE_BUZZER_TONE
  tone(OUTPUT_PIN, 1500, 300);
  delay(350);
#else
  digitalWrite(OUTPUT_PIN, HIGH); delay(300);
  digitalWrite(OUTPUT_PIN, LOW);
#endif
}

// ---- Output trigger ---------------------------------------
// Called once per valid ring event.
// RELAY: energizes for RING_DURATION ms (loop handles release).
// BUZZER_TONE: plays ding-dong synchronously then returns.
//   ringing flag still set so loop won't double-trigger.
// BUZZER_SIMPLE: turns on; loop turns off after RING_DURATION.
void triggerOutput() {
#if OUTPUT_MODE == OUTPUT_MODE_RELAY
  digitalWrite(OUTPUT_PIN, RELAY_ON);
  DBGLN("[OUT] Relay energized");
#elif OUTPUT_MODE == OUTPUT_MODE_BUZZER_TONE
  tone(OUTPUT_PIN, NOTE_DING, TONE_MS);
  delay(TONE_MS + 80);
  noTone(OUTPUT_PIN);
  delay(100);
  tone(OUTPUT_PIN, NOTE_DONG, TONE_MS);
  delay(TONE_MS + 80);
  noTone(OUTPUT_PIN);
  DBGLN("[OUT] Ding-dong played");
#elif OUTPUT_MODE == OUTPUT_MODE_BUZZER_SIMPLE
  digitalWrite(OUTPUT_PIN, HIGH);
  DBGLN("[OUT] Buzzer on");
#endif
}

// ---- ESP-NOW receive callback -----------------------------
void onReceive(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (len != sizeof(WirePacket)) {
    DBGF("[RX] Unexpected length %d (expected %d) — dropped\n",
      len, (int)sizeof(WirePacket));
    return;
  }

  uint8_t plain[AES_BLOCKLEN];
  memcpy(plain, data, AES_BLOCKLEN);
#if PLAINTEXT_DEBUG == 0
  AES_ECB_decrypt(&aesCtx, plain);
#endif

  Packet *pkt = (Packet*)plain;

  // Raw hex dump — if AES keys mismatch, type/sender will be garbage here
  DBGF("[RX] decrypted: %02X %02X %02X %02X %02X %02X %02X %02X "
       "%02X %02X %02X %02X %02X %02X %02X %02X\n",
    plain[0],plain[1],plain[2],plain[3],plain[4],plain[5],plain[6],plain[7],
    plain[8],plain[9],plain[10],plain[11],plain[12],plain[13],plain[14],plain[15]);

  DBGF("[RX] type=%d | sender=0x%08X | counter=%u | from %02X:%02X:%02X:%02X:%02X:%02X\n",
    pkt->type, pkt->sender, pkt->counter,
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  if (pkt->type != 1) {
    DBGLN("[RX] Not a RING packet — ignored");
    return;
  }

  int idx = findRemote(pkt->sender);

  // ── Pairing-mode intercept ───────────────────────────────────────────
  // Pairing mode is a toggle for BOTH directions:
  //   Known remote   → UNPAIR  (remove from whitelist)
  //   Unknown remote → PAIR    (add to whitelist, fall through to first ring)
  //
  // This lets the user manage remotes without serial access:
  //   Add:    enter pairing mode → press new remote → 3 pulses (open) + ring
  //   Remove: enter pairing mode → press paired remote → 5 pulses (unpaired)
  if (pairingMode) {
    if (idx != -1) {
      // Known remote pressed during pairing window → unpair it
      unpairRemote(idx);
      sendAck(mac, pkt->sender);  // ACK so remote LED lights as confirmation
      return;
    }
    // Unknown remote in pairing window → register and fall through to ring
    // The first ring confirms the pairing to the user at the door
    idx = registerRemote(pkt->sender, mac);
    if (idx == -1) return;
    remotes[idx].lastCounter = pkt->counter - 1;  // so delta=1 on next check
  } else if (idx == -1) {
    DBGF("[RX] Unknown sender 0x%08X — pairing mode off, dropped\n", pkt->sender);
    return;
  }

  // ── Counter validation ───────────────────────────────────────────────
  //
  // Unsigned 32-bit subtraction gives correct results for both forward
  // progress and rollback detection without any signed arithmetic.
  //
  //  delta == 0
  //    Exact duplicate — remote retried a packet already processed.
  //    ACK so remote stops retrying and enters deep sleep.
  //
  //  0 < delta <= 0xF0000000
  //    Normal forward progress (up to ~268 million presses between rings).
  //    Accept, update counter, ring.
  //
  //  delta > 0xF0000000
  //    Counter went backward by more than ~268 million.
  //    Likely cause: remote EEPROM was wiped after re-flash, resetting its
  //    counter to 1 while the receiver still stores a high watermark.
  //    Could also be a replay attack.
  //
  //    Response — three-state block timer (per-remote, RAM only):
  //      State A (blockedUntil == 0):
  //        First rollback seen → start 10-second block, suppress, log.
  //      State B (millis() < blockedUntil):
  //        Within block window → suppress, log remaining seconds.
  //        Replay attacks and button-mashing are neutralised here.
  //      State C (millis() >= blockedUntil, blockedUntil > 0):
  //        Block window has expired → auto-re-sync: accept the new counter
  //        as the new baseline and ring.  A real user who kept pressing the
  //        doorbell after a re-flash will get through after 10 s without
  //        any manual intervention.
  //        To re-sync immediately: enter pairing mode and press the remote
  //        (rollback path is bypassed entirely in pairing mode).

  uint32_t delta = pkt->counter - remotes[idx].lastCounter;

  // State: exact duplicate
  if (delta == 0) {
    DBGF("[RX] Duplicate (counter=%u, slot=%d) — ACK to stop remote retry\n",
      pkt->counter, idx);
    sendAck(mac, pkt->sender);
    return;
  }

  // State: counter rollback
  if (delta > 0xF0000000u) {
    unsigned long now = millis();

    // State A: first rollback — start block timer
    if (remotes[idx].blockedUntil == 0) {
      remotes[idx].blockedUntil = now + ROLLBACK_BLOCK_MS;
      DBGF("[RX] Rollback: remote 0x%08X slot=%d stored=%u got=%u delta=%u\n",
        pkt->sender, idx, remotes[idx].lastCounter, pkt->counter, delta);
      DBGF("[RX] Blocked for %u s — enter pairing mode to re-sync immediately\n",
        ROLLBACK_BLOCK_MS / 1000);
      sendAck(mac, pkt->sender);
      return;
    }

    // State B: still within the block window
    if (now < remotes[idx].blockedUntil) {
      unsigned long remMs = remotes[idx].blockedUntil - now;
      DBGF("[RX] 0x%08X still blocked — %lu s remaining"
           " | stored=%u got=%u\n",
        pkt->sender, remMs / 1000UL,
        remotes[idx].lastCounter, pkt->counter);
      sendAck(mac, pkt->sender);
      return;
    }

    // State C: block window expired — auto-re-sync
    DBGF("[UNBLOCK] 0x%08X: block expired — counter re-synced %u → %u"
         " (was blocked %u s)\n",
      pkt->sender, remotes[idx].lastCounter, pkt->counter,
      ROLLBACK_BLOCK_MS / 1000);
    remotes[idx].blockedUntil = 0;
    // Do NOT return — fall through and ring normally.
    // lastCounter will be updated below to pkt->counter.
  }

  // ── Valid packet — update state and ring ─────────────────────────────
  remotes[idx].lastCounter = pkt->counter;
  saveCounter(idx);

  DBGF("[RING] *** DING DONG *** remote=0x%08X | counter=%u | slot=%d\n",
    pkt->sender, pkt->counter, idx);

  triggerOutput();
  ringing   = true;
  ringStart = millis();

  sendAck(mac, pkt->sender);
}

// ---- Entry points -----------------------------------------
void setup() {
  // ── OUTPUT PIN — safe state before the driver is enabled ────────────
  // Rule: always call digitalWrite() BEFORE pinMode(OUTPUT).
  //
  // On ESP8266, calling pinMode(OUTPUT) enables the push-pull driver
  // immediately using whatever value the output latch currently holds.
  // The reset-default latch value on GPIO14 (D5) is LOW.  With
  // active-LOW relay (RELAY_OFF = HIGH) that LOW would fire the relay
  // for a few microseconds — enough to click a mechanical relay coil
  // and, if the bell circuit is sensitive, ring the chime.
  //
  // Calling digitalWrite(RELAY_OFF) first writes HIGH into the latch
  // while the pin is still an input (no current flows).  When
  // pinMode(OUTPUT) then enables the driver it comes up at HIGH =
  // RELAY_OFF from the very first clock cycle — zero-width glitch.
  //
  // This MUST be the first thing in setup(), before Serial, WiFi,
  // or any other initialisation that takes time.
#if OUTPUT_MODE == OUTPUT_MODE_RELAY
  digitalWrite(OUTPUT_PIN, RELAY_OFF);   // pre-load latch: relay released (HIGH)
#else
  digitalWrite(OUTPUT_PIN, LOW);         // pre-load latch: buzzer/LED off
#endif
  pinMode(OUTPUT_PIN, OUTPUT);           // enable driver — already at safe level
  pinMode(PAIRING_BTN_PIN, INPUT_PULLUP);

  // ── Serial ──────────────────────────────────────────────────────────
  // 74880 baud captures both the ESP8266 boot ROM output AND
  // your application prints in a single clean stream.
  // Your PlatformIO monitor_speed must match this value.
  DBG_BEGIN(74880);
  delay(100);  // let UART settle

  DBGLN("\n\n=============================");
  DBGLN(" Doorbell Receiver v1.0");
  DBGLN(" NodeMCU v3 / Lolin ESP-12F");
  DBGLN("=============================");

  // Print MAC address — copy this into remote_main.cpp receiverMac[]
  WiFi.mode(WIFI_STA);
  DBGF("  MAC Addr  : %s   <-- copy into remote receiverMac[]\n",
    WiFi.macAddress().c_str());
  DBGF("  Chip ID   : 0x%08X\n", ESP.getChipId());
  DBGF("  Free heap : %u bytes\n", ESP.getFreeHeap());
  DBGF("  Flash size: %u bytes\n", ESP.getFlashChipSize());
  DBGLN("-----------------------------");

  AES_init_ctx(&aesCtx, AES_KEY);
  DBGLN("[AES]   Key schedule expanded OK");
#if PLAINTEXT_DEBUG
  DBGLN("[AES]   *** PLAINTEXT_DEBUG=1: AES DISABLED — dev mode only ***");
#endif

  loadAllRemotes();

  wifi_set_channel(CHANNEL);
  WiFi.setOutputPower(20.5);
  DBGF("[WiFi]  Channel %d | TX power max\n", CHANNEL);

  if (esp_now_init() != 0) {
    DBGLN("[ESP-NOW] INIT FAILED — rebooting in 1s");
    delay(1000);
    ESP.restart();
  }
  DBGLN("[ESP-NOW] Init OK");

  // COMBO role — receiver must both receive RINGs and send ACKs.
  // ESP_NOW_ROLE_SLAVE is receive-only; sending from a SLAVE silently fails.
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onReceive);
  esp_now_register_send_cb(onAckSent);   // optional debug: see if ACKs leave

  // CRITICAL FIX: add all persisted remotes as ESP-NOW peers.
  // esp_now_send() silently fails if no peer entry exists for the target MAC.
  // Remote does this for the receiver at boot; receiver must do the same
  // for every remote it knows about, otherwise ACKs are never transmitted.
  int peersAdded = 0;
  for (int i = 0; i < MAX_REMOTES; i++) {
    if (remotes[i].active) {
      addRemotePeer(remotes[i].mac);
      peersAdded++;
    }
  }

  DBGF("[PEER] %d peer(s) registered from EEPROM\n", peersAdded);

  DBGLN("\n[READY] Listening for remotes");
  DBGLN("  D2 button = enter pairing mode (10s window)");
  DBGLN("=============================\n");

  // Boot-ready audio feedback — buzzer modes only.
  //
  // Relay mode deliberately has NO boot-ready signal.
  // Pulsing the relay here would ring the doorbell chime on every
  // power-up or reset — exactly the "2-3 rings on boot" symptom.
  // The serial log above is the boot confirmation for relay builds.
  // Pairing-mode entry/exit still produce relay clicks as intended.
#if OUTPUT_MODE == OUTPUT_MODE_BUZZER_TONE
  tone(OUTPUT_PIN, 1000, 100); delay(200);
  tone(OUTPUT_PIN, 1000, 100); delay(200);
#elif OUTPUT_MODE == OUTPUT_MODE_BUZZER_SIMPLE
  for (int i = 0; i < 2; i++) {
    digitalWrite(OUTPUT_PIN, HIGH); delay(100);
    digitalWrite(OUTPUT_PIN, LOW);  delay(100);
  }
#endif
}

void loop() {
  // --- Buzzer auto-off ---
  if (ringing && (millis() - ringStart > RING_DURATION)) {
#if OUTPUT_MODE == OUTPUT_MODE_RELAY
    digitalWrite(OUTPUT_PIN, RELAY_OFF);  // release relay
#elif OUTPUT_MODE == OUTPUT_MODE_BUZZER_SIMPLE
    digitalWrite(OUTPUT_PIN, LOW);
#endif
    // BUZZER_TONE completes synchronously in triggerOutput() — nothing to do here
    ringing = false;
    DBGLN("[RING]  Output off");
  }

  // --- Pairing button (debounced, active LOW) ---
  static unsigned long lastBtnTime = 0;
  static bool lastBtnPressed = false;
  bool btnPressed = (digitalRead(PAIRING_BTN_PIN) == LOW);
  if (btnPressed && !lastBtnPressed) {
    unsigned long now = millis();
    if (now - lastBtnTime > 300) {
      lastBtnTime = now;
      pairingMode ? exitPairingMode() : enterPairingMode();
    }
  }
  lastBtnPressed = btnPressed;

  // --- Pairing auto-expire ---
  if (pairingMode && (millis() - pairingStart > PAIRING_WINDOW)) {
    exitPairingMode();
  }
}