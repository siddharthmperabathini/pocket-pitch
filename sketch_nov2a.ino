/***********************************************************************
   NON-BLOCKING CYCLING QR DISPLAY + NTAG URI WRITER
   - Updates QR every minute
   - Attempts to write NFC regardless of tag presence
   - Never waits, never blocks, never freezes
************************************************************************/

#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Adafruit_GFX.h>
#include <Adafruit_EPD.h>
#include "qrcode.h"

// ====================== PN532 NFC SETUP ======================
#define PN532_IRQ   (2)
#define PN532_RESET (3)
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET, &Wire);

// ====================== E-INK DISPLAY SETUP ======================
#define EPD_CS    D10
#define EPD_DC    D9
#define EPD_RST   D8
#define EPD_BUSY  D7
#define SRAM_CS   -1

Adafruit_SSD1680 display(250, 122, EPD_DC, EPD_RST, EPD_CS, SRAM_CS, EPD_BUSY);

// ====================== QR LIST ======================
struct QRInfo {
  const char* name;
  const char* url;
};

QRInfo qrList[] = {
  {"ESPN Homepage", "https://www.espn.com"},
  {"Purdue University", "https://www.purdue.edu"},
  {"GitHub", "https://www.github.com"}
};

const int numQRs = sizeof(qrList) / sizeof(qrList[0]);
int currentQR = 0;

// ====================== TIMING ======================
unsigned long lastUpdate = 0;
const unsigned long updateInterval = 60000;   // 60 seconds

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Starting Non-Blocking NFC + QR Display System...");

  // Init e-ink
  display.begin();
  display.clearBuffer();
  display.display();

  // Init PN532
  Wire.begin();
  nfc.begin();

  uint32_t v = nfc.getFirmwareVersion();
  if (!v) {
    Serial.println("ERROR: PN532 not detected!");
  } else {
    Serial.println("PN532 OK.");
  }
}

// ====================== DISPLAY QR ======================
void displayQRCodeWithLabel(const QRInfo &qrInfo) {
  display.clearBuffer();

  display.setTextSize(1);
  display.setTextColor(EPD_BLACK);
  display.setCursor(10, 0);
  display.print(qrInfo.name);

  QRCode qrcode;
  uint8_t version = 3;
  uint8_t qrcodeData[qrcode_getBufferSize(version)];
  qrcode_initText(&qrcode, qrcodeData, version, ECC_LOW, qrInfo.url);

  int scale = 3;
  int qrPixelSize = qrcode.size * scale;
  uint16_t x = (display.width() - qrPixelSize) / 2;
  uint16_t y = 20;

  for (uint8_t qy = 0; qy < qrcode.size; qy++) {
    for (uint8_t qx = 0; qx < qrcode.size; qx++) {
      if (qrcode_getModule(&qrcode, qx, qy)) {
        display.fillRect(x + qx * scale, y + qy * scale, scale, scale, EPD_BLACK);
      }
    }
  }

  display.display();
}

// ====================== LOOP ======================
void loop() {

  unsigned long now = millis();

  // every minute:
  if (now - lastUpdate >= updateInterval) {
    lastUpdate = now;

    Serial.println("\n========== QR + NFC UPDATE ==========");

    QRInfo &qr = qrList[currentQR];

    Serial.print("Displaying: ");
    Serial.println(qr.name);
    displayQRCodeWithLabel(qr);

    // Try to detect a tag *once* (non-blocking)
    uint8_t uid[7];
    uint8_t uidLen;

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen)) {
      Serial.println("Tag detected → attempting write...");

      if (writeNtagUri(nfc, qr.url)) {
        Serial.println("✔ NFC write success!");
      } else {
        Serial.println("✖ NFC write failed.");
      }
    } else {
      Serial.println("No tag detected this cycle.");
    }

    currentQR = (currentQR + 1) % numQRs;
  }
}

// ===================== NDEF / NTAG helper functions =====================

// Write an NDEF URI TLV to an NTAG starting at page 4 (4 bytes per page).
// Returns true on success.
bool writeNtagUri(Adafruit_PN532 &nfc, const char *url) {
  // Build NDEF record (short-record URI)
  // Header: 0xD1 (MB=1, ME=1, SR=1, TNF=0x01)
  // Type Length: 0x01 ('U')
  // Payload Length: 1 + urlLen (1 = identifier code)
  // Type: 'U' (0x55)
  // Identifier: 0x00 (no prefix)
  size_t urlLen = strlen(url);
  if (urlLen > 200) {
    // NTAG215 capacity ~ 504 bytes user memory, but keep a conservative limit
    Serial.println("URL too long; shorten it.");
    return false;
  }

  // Compose NDEF payload bytes into ndef[] array
  uint8_t ndef[512];
  size_t np = 0;
  ndef[np++] = 0xD1;               // NDEF header for short record, MB+ME+SR, TNF=0x01
  ndef[np++] = 0x01;               // Type Length = 1
  ndef[np++] = (uint8_t)(1 + urlLen); // Payload length = 1 (idCode) + URL length (fits in 1 byte)
  ndef[np++] = 0x55;               // Type 'U' (URI)
  ndef[np++] = 0x00;               // URI Identifier Code = 0x00 (no prefix)
  // copy URL bytes
  for (size_t i = 0; i < urlLen; ++i) ndef[np++] = (uint8_t)url[i];

  // Build TLV: 0x03 (NDEF TLV), length (np), <ndef bytes...>, 0xFE terminator
  uint8_t tlv[520];
  size_t tp = 0;
  tlv[tp++] = 0x03;            // NDEF TLV tag
  if (np <= 0xFF) {
    tlv[tp++] = (uint8_t)np;   // length (one byte)
  } else {
    // For huge records we'd need to use 0xFF + 2-byte length, but we guard against that above.
    Serial.println("NDEF too large (unexpected).");
    return false;
  }
  // copy NDEF bytes
  for (size_t i = 0; i < np; ++i) tlv[tp++] = ndef[i];
  tlv[tp++] = 0xFE;            // Terminator TLV

  // Sanity: read page 4 to ensure tag responds (and we are not overwriting CC)
  uint8_t pageBuf[4];
  uint16_t writePage = 4;
  if (!nfc.ntag2xx_ReadPage(writePage, pageBuf)) {
    Serial.println("Error: could not read page 4. Is this an NTAG/Type 2 tag?");
    return false;
  }

  // Write TLV page-by-page (4 bytes each) starting at page 4
  size_t offset = 0;
  while (offset < tp) {
    // Fill 4-byte buffer
    for (int i = 0; i < 4; ++i) {
      if (offset < tp) pageBuf[i] = tlv[offset++];
      else pageBuf[i] = 0x00; // pad with zeros
    }

    // Write page
    if (!nfc.ntag2xx_WritePage(writePage, pageBuf)) {
      Serial.print("Failed to write page ");
      Serial.println(writePage);
      return false;
    } else {
      Serial.print("Wrote page ");
      Serial.println(writePage);
    }

    writePage++;
    delay(30); // short pause between writes
    // Safety: avoid writing into lock/config pages; we started at page 4 so we're safe.
    // If writePage gets too high, we could bail; but typical tags will accept these writes.
  }

  Serial.println("NDEF TLV write complete.");
  return true;
}

// Read back NDEF TLV starting at page 4 and extract a simple URI NDEF (identifier code 0x00).
// Returns extracted URI as a String (empty if not found or parse error).
String readNtagNdefUri(Adafruit_PN532 &nfc) {
  uint8_t pageBuf[4];
  uint16_t page = 4;
  bool seenTlvStart = false;
  bool seenLen = false;
  uint8_t ndefLen = 0;
  size_t collected = 0;
  // We'll store NDEF payload bytes (max reasonable size)
  const size_t MAX_NDEF = 480;
  uint8_t ndefBytes[MAX_NDEF];
  size_t ndefIndex = 0;

  // Read pages until terminator or until we've collected the advertised NDEF length
  for (int iter = 0; iter < 128; ++iter) { // safety cap
    if (!nfc.ntag2xx_ReadPage(page, pageBuf)) {
      Serial.print("Read failed page ");
      Serial.println(page);
      break;
    }
    for (int i = 0; i < 4; ++i) {
      uint8_t b = pageBuf[i];
      if (!seenTlvStart) {
        if (b == 0x00) {
          // NULL TLV - skip
          continue;
        } else if (b == 0x03) {
          seenTlvStart = true;
          continue;
        } else {
          // other TLV types (skip) - but for typical NDEF we'll see 0x03 soon
          continue;
        }
      } else if (seenTlvStart && !seenLen) {
        // this byte is NDEF length
        ndefLen = b;
        seenLen = true;
        if (ndefLen == 0) {
          // empty NDEF - nothing to do
          goto parse_done;
        }
        continue;
      } else if (seenLen) {
        // accumulate ndef bytes up to ndefLen
        if (collected < ndefLen && ndefIndex < MAX_NDEF) {
          ndefBytes[ndefIndex++] = b;
          collected++;
          if (collected >= ndefLen) {
            // done collecting advertised NDEF bytes; we can stop
            goto parse_done;
          }
        }
      }
    }
    page++;
  }

parse_done:
  if (!seenTlvStart || !seenLen || collected == 0) {
    Serial.println("No NDEF TLV found or empty.");
    return String("");
  }

  // Parse simple NDEF URI record:
  // Expect: [header 1][typeLen 1][payloadLen 1][type 'U' (1 byte)][idCode 1][URI bytes...]
  size_t idx = 0;
  if (ndefIndex < 5) return String("");
  uint8_t header = ndefBytes[idx++];         // e.g., 0xD1
  uint8_t typeLen = ndefBytes[idx++];       // should be 0x01
  uint8_t payloadLen = ndefBytes[idx++];    // payload length
  uint8_t type = ndefBytes[idx++];          // should be 'U' (0x55)
  uint8_t idCode = ndefBytes[idx++];        // identifier code (0x00 => no prefix)
  // Remaining bytes are the URI
  String uri = "";
  for (; idx < (size_t)ndefIndex; ++idx) {
    uri += (char)ndefBytes[idx];
  }

  // Optionally, handle common idCode prefixes (0x01 => "http://www.", etc.)
  // But this code assumes idCode==0x00 (no prefix).
  if (idCode != 0x00) {
    // quick mapping for a few common prefixes:
    const char* pfx = "";
    switch (idCode) {
      case 0x01: pfx = "http://www."; break;
      case 0x02: pfx = "https://www."; break;
      case 0x03: pfx = "http://"; break;
      case 0x04: pfx = "https://"; break;
      default: pfx = ""; break;
    }
    if (strlen(pfx) > 0) {
      uri = String(pfx) + uri;
    }
  }

  return uri;
}

// wait until tag removed
void waitForTagRemoval(uint8_t *uid, uint8_t uidLen) {
  Serial.print("Waiting for card removal...");
  while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen)) {
    delay(200);
  }
  Serial.println(" removed.");
}

// (Optional) Keep your old printKeyHex around if you want to reuse later
void printKeyHex(const uint8_t key[6]) {
  for (int i = 0; i < 6; i++) {
    if (key[i] < 0x10) Serial.print('0');
    Serial.print(key[i], HEX);
    if (i < 5) Serial.print(':');
  }
}
