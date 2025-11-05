#include <Adafruit_GFX.h>
#include <Adafruit_EPD.h>
#include "qrcode.h"

// Your pin assignments
#define EPD_CS    D10
#define EPD_DC    D9
#define EPD_RST   D8
#define EPD_BUSY  D7
#define SRAM_CS   -1

// Instantiate the display object
Adafruit_SSD1680 display(250, 122, EPD_DC, EPD_RST, EPD_CS, SRAM_CS, EPD_BUSY);

// Array of QR codes and corresponding names
struct QRInfo {
  const char* name;  // Label to show above QR
  const char* url;   // QR code content
};

QRInfo qrList[] = {
  {"ESPN Homepage", "https://www.espn.com"},
  {"Purdue University", "https://www.purdue.edu"},
  {"GitHub", "https://www.github.com"}
};

const int numQRs = sizeof(qrList) / sizeof(qrList[0]);
int currentQR = 0; // index of current QR code

void setup() {
  Serial.begin(115200);
  delay(2000); // Wait 2 seconds for serial connection

  Serial.println("Initializing display...");
  display.begin();
  display.clearBuffer();
  Serial.println("Setup complete!");
}

void displayQRCodeWithLabel(const QRInfo &qrInfo) {
  display.clearBuffer();  // Clear previous QR and text

  // Draw label at top
  display.setTextSize(1);  // Small text size
  display.setTextColor(EPD_BLACK);
  display.setCursor(10, 0); // x=10, y=0 (top margin)
  display.print(qrInfo.name);

  // Generate QR code
  QRCode qrcode;
  uint8_t version = 3;
  uint8_t qrcodeData[qrcode_getBufferSize(version)];
  qrcode_initText(&qrcode, qrcodeData, version, ECC_LOW, qrInfo.url);

  int scale = 3;
  int qrPixelSize = qrcode.size * scale;

  // Draw QR code centered horizontally, leaving space for label
  uint16_t x = (display.width() - qrPixelSize) / 2;
  uint16_t y = 20; // start below label

  for (uint8_t qy = 0; qy < qrcode.size; qy++) {
    for (uint8_t qx = 0; qx < qrcode.size; qx++) {
      if (qrcode_getModule(&qrcode, qx, qy)) {
        display.fillRect(x + qx * scale, y + qy * scale, scale, scale, EPD_BLACK);
      }
    }
  }

  display.display();
  Serial.print("Displayed QR code: ");
  Serial.println(qrInfo.url);
}

void loop() {
  // Rotate QR codes every 5 seconds
  displayQRCodeWithLabel(qrList[currentQR]);
  currentQR = (currentQR + 1) % numQRs;  // go to next QR code
  delay(60000);  // wait 5 seconds before rotating
}
