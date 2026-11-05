
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <esp_arduino_version.h>
#include <esp_now.h>


// =====================================================
// 1. AD8232 ECG PINS
// =====================================================

const int ECG_PIN = 32;
const int LO_MINUS = 35;
const int LO_PLUS = 34;


// =====================================================
// 2. ESP-NOW DATA
// The sender MUST use the same structure.
// ========================================= ============

struct VitalPacket {
  float temperature;
  float spo2;
  float heartRate;
};

VitalPacket receivedVitals = {0, 0, 0};

portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;

volatile bool haveReceivedData = false;
volatile uint32_t lastPacketMillis = 0;

// =====================================================
// 3. TFT CONFIGURATION: ILI9488, 480 x 320
// =====================================================

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9488 _panel;
  lgfx::Bus_SPI _bus;

public:
  LGFX() {
    {
      auto cfg = _bus.config();

      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 10000000;
      cfg.freq_read = 5000000;

      cfg.pin_sclk = 18;
      cfg.pin_mosi = 23;
      cfg.pin_miso = -1;
      cfg.pin_dc = 2;

      _bus.config(cfg);
      _panel.setBus(&_bus);
    }

    {
      auto cfg = _panel.config();

      cfg.pin_cs = 5;
      cfg.pin_rst = 4;
      cfg.pin_busy = -1;

      cfg.memory_width = 320;
      cfg.memory_height = 480;
      cfg.panel_width = 320;
      cfg.panel_height = 480;

      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;

      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;

      _panel.config(cfg);
    }

    setPanel(&_panel);
  }
};

LGFX lcd;

// =====================================================
// 4. DISPLAY LAYOUT
// =====================================================

const int GRAPH_LEFT = 25;
const int GRAPH_RIGHT = 465;
const int GRAPH_TOP = 170;
const int GRAPH_BOTTOM = 285;

const int GRAPH_WIDTH = GRAPH_RIGHT - GRAPH_LEFT;

int graphX = GRAPH_LEFT;
int previousY = -1;

uint32_t lastDisplayUpdate = 0;

// =====================================================
// 5. ESP-NOW RECEIVE CALLBACK
// =====================================================

void processReceivedData(const uint8_t *data, int len) {
  if (len != sizeof(VitalPacket)) {
    return;
  }

  VitalPacket incoming;
  memcpy(&incoming, data, sizeof(incoming));

  portENTER_CRITICAL(&dataMux);
  receivedVitals = incoming;
  lastPacketMillis = millis();
  haveReceivedData = true;
  portEXIT_CRITICAL(&dataMux);
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  processReceivedData(data, len);
}

#else

void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  processReceivedData(data, len);
}

#endif

// =====================================================
// 6. DRAW THE DASHBOARD
// =====================================================

void drawDashboard() {
  lcd.fillScreen(TFT_BLACK);

  // Header
  lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setCursor(15, 10);
  lcd.print("CONNECTED PATIENT MONITOR");

  lcd.drawFastHLine(15, 38, 450, TFT_DARKGREY);

  // Vital-sign cards
  lcd.drawRoundRect(10, 48, 148, 76, 8, TFT_DARKGREY);
  lcd.drawRoundRect(166, 48, 148, 76, 8, TFT_DARKGREY);
  lcd.drawRoundRect(322, 48, 148, 76, 8, TFT_DARKGREY);

  lcd.setTextSize(1);

  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.setCursor(20, 58);
  lcd.print("TEMPERATURE");

  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  lcd.setCursor(176, 58);
  lcd.print("OXYGEN SpO2");

  lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  lcd.setCursor(332, 58);
  lcd.print("HEART RATE");

  // ECG graph frame
  lcd.drawRoundRect(15, 145, 450, 155, 5, TFT_DARKGREY);

  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  lcd.setTextSize(1);
  lcd.setCursor(25, 151);
  lcd.print("ECG / AD8232");

  // Horizontal graph grid
  for (int y = GRAPH_TOP; y <= GRAPH_BOTTOM; y += 23) {
    lcd.drawFastHLine(GRAPH_LEFT, y, GRAPH_WIDTH, 0x2104);
  }

  // Vertical graph grid
  for (int x = GRAPH_LEFT; x <= GRAPH_RIGHT; x += 44) {
    lcd.drawFastVLine(x, GRAPH_TOP, GRAPH_BOTTOM - GRAPH_TOP, 0x2104);
  }

  // Footer
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.setCursor(20, 306);
  lcd.print("APTX 4869");
}

// =====================================================
// 7. UPDATE VITAL-SIGN VALUES
// =====================================================

void updateVitalDisplay() {
  VitalPacket snapshot;
  bool dataAvailable;
  uint32_t packetTime;

  portENTER_CRITICAL(&dataMux);
  snapshot = receivedVitals;
  dataAvailable = haveReceivedData;
  packetTime = lastPacketMillis;
  portEXIT_CRITICAL(&dataMux);

  bool wirelessConnected = dataAvailable && (millis() - packetTime < 5000);

  // Erase the previous values, not the whole screen.
  lcd.fillRect(18, 76, 132, 35, TFT_BLACK);
  lcd.fillRect(174, 76, 132, 35, TFT_BLACK);
  lcd.fillRect(330, 76, 132, 35, TFT_BLACK);

  lcd.setTextSize(2);

  if (wirelessConnected) {
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.setCursor(20, 82);
    lcd.printf("%.1f C", snapshot.temperature);

    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.setCursor(176, 82);
    lcd.printf("%.1f %%", snapshot.spo2);

    lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    lcd.setCursor(332, 82);
    lcd.printf("%.1f BPM", snapshot.heartRate);
  } else {
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_RED, TFT_BLACK);

    lcd.setCursor(20, 87);
    lcd.print("Patient not");

    lcd.setCursor(20, 99);
    lcd.print("connected yet");

    lcd.setCursor(176, 87);
    lcd.print("Patient not");

    lcd.setCursor(176, 99);
    lcd.print("connected yet");

    lcd.setCursor(332, 87);
    lcd.print("Patient not");

    lcd.setCursor(332, 99);
    lcd.print("connected yet");
  }

  // Update the ECG electrode status.
  lcd.fillRect(125, 151, 320, 13, TFT_BLACK);
  lcd.setTextSize(1);

  bool leadsOff = digitalRead(LO_PLUS) == HIGH || digitalRead(LO_MINUS) == HIGH;

  if (leadsOff) {
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.setCursor(130, 151);
    lcd.print("Patient not connected yet");
  } else {
    lcd.setTextColor(TFT_GREEN, TFT_BLACK);
    lcd.setCursor(130, 151);
    lcd.print("ECG ELECTRODES CONNECTED");
  }
}

// =====================================================
// 8. PLOT ONE ECG SAMPLE
// =====================================================

void plotECG(int ecgValue) {
  bool leadsOff = digitalRead(LO_PLUS) == HIGH || digitalRead(LO_MINUS) == HIGH;

  // Erase the old trace at this horizontal position.
  // Restore grid lines underneath it.
  lcd.drawFastVLine(graphX, GRAPH_TOP + 1, GRAPH_BOTTOM - GRAPH_TOP - 1,
                    TFT_BLACK);

  if ((graphX - GRAPH_LEFT) % 44 == 0) {
    lcd.drawFastVLine(graphX, GRAPH_TOP + 1, GRAPH_BOTTOM - GRAPH_TOP - 1,
                      0x2104);
  }

  if (!leadsOff) {
    // Map ADC readings to graph height.
    // Adjust this range if your ECG trace is too small
    // or clips at the top and bottom.
    int y = map(ecgValue, 1200, 2800, GRAPH_BOTTOM - 3, GRAPH_TOP + 3);

    y = constrain(y, GRAPH_TOP + 2, GRAPH_BOTTOM - 2);

    if (previousY >= GRAPH_TOP && previousY <= GRAPH_BOTTOM) {
      lcd.drawLine(graphX - 1, previousY, graphX, y, TFT_GREEN);
    }

    previousY = y;
  } else {
    previousY = -1;
  }

  graphX++;

  if (graphX >= GRAPH_RIGHT) {
    graphX = GRAPH_LEFT;
    previousY = -1;
  }
}

// =====================================================
// 9. SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  pinMode(LO_PLUS, INPUT);
  pinMode(LO_MINUS, INPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(ECG_PIN, ADC_11db);

  // Start the TFT.
  lcd.init();
  lcd.setRotation(1);
  lcd.setColorDepth(16);
  lcd.setBrightness(255);

  drawDashboard();

  // Start ESP-NOW reception.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print("Receiver MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW initialization failed!");
  } else {
    esp_now_register_recv_cb(OnDataRecv);
    Serial.println("ESP-NOW receiver ready.");
  }
}

// =====================================================
// 10. MAIN LOOP
// =====================================================

void loop() {
  // Read the ECG continuously.
  int ecgValue = analogRead(ECG_PIN);

  int leadOffPlus = digitalRead(LO_PLUS);
  int leadOffMinus = digitalRead(LO_MINUS);

  // Preserve your original Serial Plotter format.
  Serial.print(">ecg:");
  Serial.print(ecgValue);
  Serial.print(",lo_plus:");
  Serial.print(leadOffPlus);
  Serial.print(",lo_minus:");
  Serial.println(leadOffMinus);

  // Update the scrolling waveform.
  plotECG(ecgValue);

  // Refresh text periodically instead of every sample.
  if (millis() - lastDisplayUpdate >= 500) {
    lastDisplayUpdate = millis();
    updateVitalDisplay();
  }

  // Approximate 250 ECG samples per second.
  delay(4);
}
