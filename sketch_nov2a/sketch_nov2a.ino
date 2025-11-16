/***********************************************************************
   SERIAL-DRIVEN QR DISPLAY + NTAG URI WRITER (WITH DEBUG)
   - Starts with default URL (Purdue)
   - User can type any URL in Serial Monitor to update QR + NFC
   - Comprehensive debug output throughout
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

// ====================== CURRENT URL ======================
String currentURL = "https://www.purdue.edu";  // default

// ====================== NFC WRITE FUNCTION ======================
// Write an NDEF URI TLV to an NTAG starting at page 4 (4 bytes per page).
// Returns true on success.
bool writeNtagUri(Adafruit_PN532 &nfc, const char *url) {
  Serial.println("\n--- writeNtagUri() called ---");
  Serial.print("URL to write: ");
  Serial.println(url);
  
  // Build NDEF record (short-record URI)
  // Header: 0xD1 (MB=1, ME=1, SR=1, TNF=0x01)
  // Type Length: 0x01 ('U')
  // Payload Length: 1 + urlLen (1 = identifier code)
  // Type: 'U' (0x55)
  // Identifier: 0x00 (no prefix - full URL)
  size_t urlLen = strlen(url);
  Serial.print("URL length: ");
  Serial.println(urlLen);
  
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
  ndef[np++] = (uint8_t)(1 + urlLen); // Payload length = 1 (idCode) + URL length
  ndef[np++] = 0x55;               // Type 'U' (URI)
  ndef[np++] = 0x00;               // URI Identifier Code = 0x00 (no prefix - full URL)
  
  Serial.println("Building NDEF record...");
  Serial.print("NDEF header bytes: 0xD1 0x01 0x");
  if (1 + urlLen < 0x10) Serial.print("0");
  Serial.print(1 + urlLen, HEX);
  Serial.println(" 0x55 0x00");
  
  // copy URL bytes
  for (size_t i = 0; i < urlLen; ++i) {
    ndef[np++] = (uint8_t)url[i];
  }

  Serial.print("Total NDEF record size: ");
  Serial.print(np);
  Serial.println(" bytes");

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
  for (size_t i = 0; i < np; ++i) {
    tlv[tp++] = ndef[i];
  }
  tlv[tp++] = 0xFE;            // Terminator TLV

  Serial.print("Total TLV size (with terminator): ");
  Serial.print(tp);
  Serial.println(" bytes");
  
  Serial.println("\nTLV structure:");
  Serial.print("  TLV Tag: 0x03, Length: 0x");
  if (np < 0x10) Serial.print("0");
  Serial.print(np, HEX);
  Serial.println(", Terminator: 0xFE");

  // Sanity: read page 4 to ensure tag responds
  Serial.println("\nVerifying tag is writable...");
  uint8_t pageBuf[4];
  uint16_t writePage = 4;
  
  if (!nfc.ntag2xx_ReadPage(writePage, pageBuf)) {
    Serial.println("✗ Error: could not read page 4. Is this an NTAG/Type 2 tag?");
    return false;
  }
  Serial.println("✓ Tag responds correctly");

  // Write TLV page-by-page (4 bytes each) starting at page 4
  Serial.println("\nWriting NDEF message to tag...");
  size_t offset = 0;
  
  while (offset < tp) {
    // Fill 4-byte buffer
    for (int i = 0; i < 4; ++i) {
      if (offset < tp) {
        pageBuf[i] = tlv[offset++];
      } else {
        pageBuf[i] = 0x00; // pad with zeros
      }
    }

    // Write page
    Serial.print("Writing page ");
    Serial.print(writePage);
    Serial.print(": ");
    for (int i = 0; i < 4; i++) {
      Serial.print("0x");
      if (pageBuf[i] < 0x10) Serial.print("0");
      Serial.print(pageBuf[i], HEX);
      Serial.print(" ");
    }
    
    if (!nfc.ntag2xx_WritePage(writePage, pageBuf)) {
      Serial.println("✗ FAILED");
      Serial.print("Failed to write page ");
      Serial.println(writePage);
      return false;
    } else {
      Serial.println("✓");
    }

    writePage++;
    delay(30); // short pause between writes
  }

  Serial.println("\n✔ NDEF TLV write complete!");
  Serial.print("Wrote ");
  Serial.print(writePage - 4);
  Serial.println(" pages total");
  
  return true;
}

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);
  delay(2000);  // Give serial time to initialize
  
  Serial.println("\n╔════════════════════════════════════════════════╗");
  Serial.println("║  Serial-Driven NFC + QR Display System       ║");
  Serial.println("╚════════════════════════════════════════════════╝\n");
  
  Serial.println("📋 Instructions:");
  Serial.println("   Type a URL in Serial Monitor and press ENTER");
  Serial.println("   The QR code will update immediately");
  Serial.println("   Place an NFC tag to write the URL\n");
  
  // Init e-ink
  Serial.print("Initializing e-ink display... ");
  display.begin();
  display.setRotation(0);
  display.clearBuffer();
  display.display();
  Serial.println("✓ Done");
  
  // Init I2C
  Serial.print("Initializing I2C bus... ");
  Wire.begin();
  Serial.println("✓ Done");
  
  // Init PN532
  Serial.print("Initializing PN532 NFC module... ");
  nfc.begin();
  
  uint32_t v = nfc.getFirmwareVersion();
  if (!v) {
    Serial.println("✗ FAILED");
    Serial.println("ERROR: PN532 not detected!");
    Serial.println("Check wiring and connections.");
  } else {
    Serial.println("✓ Done");
    Serial.print("   Firmware version: 0x");
    Serial.println(v, HEX);
  }
  
  // Configure PN532 to read RFID tags
  Serial.print("Configuring PN532 for tag reading... ");
  nfc.SAMConfig();
  Serial.println("✓ Done");
  
  // Display initial QR
  Serial.println("\nDisplaying default QR code...");
  displayQRCodeWithLabel("Default", currentURL.c_str());
  Serial.println("✓ Default QR code displayed\n");
  
  Serial.println("════════════════════════════════════════════════");
  Serial.println("System ready! Waiting for input...\n");
}

// ====================== DISPLAY QR ======================
void displayQRCodeWithLabel(const char* name, const char* url) {
  Serial.println("\n--- displayQRCodeWithLabel() called ---");
  Serial.print("Label: ");
  Serial.println(name);
  Serial.print("URL: ");
  Serial.println(url);
  
  Serial.print("Clearing display buffer... ");
  display.clearBuffer();
  Serial.println("✓");
  
  // Draw header bar
  Serial.print("Drawing header bar... ");
  display.fillRect(0, 0, display.width(), 2, EPD_BLACK);
  Serial.println("✓");
  
  // Draw label text
  Serial.print("Drawing label text... ");
  display.setTextSize(2);
  display.setTextColor(EPD_BLACK);
  display.setCursor(10, -4);
  display.print(name);
  Serial.println("✓");
  
  // Generate QR code
  Serial.print("Generating QR code... ");
  QRCode qrcode;
  uint8_t version = 3;
  uint8_t qrcodeData[qrcode_getBufferSize(version)];
  qrcode_initText(&qrcode, qrcodeData, version, ECC_LOW, url);
  Serial.println("✓");
  
  Serial.print("QR code size: ");
  Serial.print(qrcode.size);
  Serial.println(" modules");
  
  // Draw QR code
  Serial.print("Rendering QR code to display... ");
  int scale = 3;
  int qrPixelSize = qrcode.size * scale;
  uint16_t x = (display.width() - qrPixelSize) / 2;
  uint16_t y = 16;
  
  Serial.print("\n   Position: (");
  Serial.print(x);
  Serial.print(", ");
  Serial.print(y);
  Serial.print("), Scale: ");
  Serial.println(scale);
  
  for (uint8_t qy = 0; qy < qrcode.size; qy++) {
    for (uint8_t qx = 0; qx < qrcode.size; qx++) {
      if (qrcode_getModule(&qrcode, qx, qy)) {
        display.fillRect(x + qx * scale, y + qy * scale, scale, scale, EPD_BLACK);
      }
    }
  }
  Serial.println("✓");
  
  // Update display
  Serial.print("Updating e-ink display (this may take a few seconds)... ");
  display.display();
  Serial.println("✓");
  Serial.println("Display update complete!\n");
}

// ====================== LOOP ======================
void loop() {
  // Check for serial input (non-blocking)
  if (Serial.available()) {
    Serial.println("\n📥 Serial data detected!");
    
    String incoming = Serial.readStringUntil('\n');
    incoming.trim();
    
    Serial.print("Raw input: '");
    Serial.print(incoming);
    Serial.println("'");
    Serial.print("Length: ");
    Serial.println(incoming.length());
    
    if (incoming.length() > 0) {
      Serial.println("\n════════════════════════════════════════════════");
      Serial.println("🔄 Processing URL update...");
      Serial.println("════════════════════════════════════════════════");
      
      currentURL = incoming;
      Serial.print("✓ New URL stored: ");
      Serial.println(currentURL);
      
      // Update QR code
      Serial.println("\n📱 Updating QR code display...");
      displayQRCodeWithLabel("Custom URL", currentURL.c_str());
      
      // Try to detect and write to NFC tag
      Serial.println("\n🔍 Checking for NFC tag...");
      Serial.println("   Place tag near reader now...");
      
      uint8_t uid[7];
      uint8_t uidLen;
      
      // Try to read tag (with timeout)
      bool tagFound = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 100);
      
      if (tagFound) {
        Serial.println("✓ NFC tag detected!");
        Serial.print("   UID: ");
        for (uint8_t i = 0; i < uidLen; i++) {
          Serial.print("0x");
          if (uid[i] < 0x10) Serial.print("0");
          Serial.print(uid[i], HEX);
          if (i < uidLen - 1) Serial.print(" ");
        }
        Serial.println();
        Serial.print("   UID Length: ");
        Serial.println(uidLen);
        
        Serial.println("\n✍️ Writing URL to NFC tag...");
        if (writeNtagUri(nfc, currentURL.c_str())) {
          Serial.println("\n╔════════════════════════════════════════════════╗");
          Serial.println("║  ✔ SUCCESS: NFC write completed!             ║");
          Serial.println("╚════════════════════════════════════════════════╝");
        } else {
          Serial.println("\n╔════════════════════════════════════════════════╗");
          Serial.println("║  ✖ ERROR: NFC write failed                    ║");
          Serial.println("╚════════════════════════════════════════════════╝");
          Serial.println("Possible causes:");
          Serial.println("   - Tag removed too quickly");
          Serial.println("   - Tag is not writable (locked)");
          Serial.println("   - Wrong tag type (needs NTAG213/215/216)");
        }
      } else {
        Serial.println("⚠ No NFC tag detected");
        Serial.println("   To write to NFC, place tag near reader");
        Serial.println("   and enter the URL again");
      }
      
      Serial.println("\n════════════════════════════════════════════════");
      Serial.println("Ready for next command!\n");
      
    } else {
      Serial.println("⚠ Empty input ignored\n");
    }
  }
  
  // Small delay to prevent overwhelming the serial buffer
  delay(10);
}