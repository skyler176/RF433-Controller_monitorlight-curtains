#include <Arduino.h>
#include <M5Unified.h>
#include <SPIFFS.h>
#include <driver/rmt_tx.h>

namespace {

constexpr gpio_num_t kRfTxPin = GPIO_NUM_9;
constexpr gpio_num_t kRfRxPin = GPIO_NUM_8;
constexpr uint32_t kCaptureMagic = 0x43583952;
constexpr uint16_t kCaptureVersion = 1;
constexpr uint32_t kBaselineMs = 1000;
constexpr uint32_t kSignalMs = 2500;
constexpr uint16_t kStoredMinPulseUs = 150;
constexpr uint16_t kStoredMaxPulseUs = 20000;
constexpr size_t kMaxStoredEdges = 5120;
constexpr uint8_t kSamplesPerAction = 10;
constexpr uint32_t kRmtResolutionHz = 1000000;
constexpr uint16_t kZeroHighUs = 173;
constexpr uint16_t kZeroLowUs = 547;
constexpr uint16_t kOneHighUs = 513;
constexpr uint16_t kOneLowUs = 202;
constexpr uint16_t kFrameGapUs = 3149;
constexpr uint8_t kCodeBits = 24;
constexpr uint8_t kReplayRepeats = 5;

struct Action {
  const char* id;
  const char* label;
  const char* instruction;
  uint32_t replayCode;
};

constexpr Action kActions[] = {
    {"power", "POWER", "Press and hold POWER", 0x162A5E},
    {"brightness_cw", "BRIGHT CW", "Turn brightness CW one step", 0x162AAA},
    {"brightness_ccw", "BRIGHT CCW", "Turn brightness CCW one step", 0x162A5D},
    {"cct_cw", "CCT CW", "Turn CCT CW one step", 0x162AAF},
    {"cct_ccw", "CCT CCW", "Turn CCT CCW one step", 0x162A5F},
    {"preset_s1", "PRESET S1", "Press and hold S1", 0x162AC5},
    {"preset_s2", "PRESET S2", "Press and hold S2", 0x162A5B},
    {"preset_s3", "PRESET S3", "Press and hold S3", 0x162AA5},
};
constexpr size_t kActionCount = sizeof(kActions) / sizeof(kActions[0]);

struct EdgeStats {
  uint32_t edges = 0;
  uint32_t minUs = UINT32_MAX;
  uint32_t maxUs = 0;
  uint32_t under150Us = 0;
  uint32_t from150To1000Us = 0;
  uint32_t over1000Us = 0;
};

struct __attribute__((packed)) EdgeSample {
  uint16_t durationUs;
  uint8_t level;
};

struct CaptureHeader {
  uint32_t magic;
  uint16_t version;
  uint8_t actionIndex;
  uint8_t sampleNumber;
  uint32_t baselineMs;
  uint32_t signalMs;
  EdgeStats baseline;
  EdgeStats signal;
  uint32_t totalStoredEligibleEdges;
  uint16_t storedEdgeCount;
  uint8_t ringOverflow;
  uint8_t reserved;
};

enum class CapturePhase { Idle, Baseline, Signal };

volatile EdgeStats liveStats;
volatile EdgeSample edgeRing[kMaxStoredEdges];
EdgeSample orderedEdges[kMaxStoredEdges];
volatile uint32_t edgeRingTotal = 0;
volatile uint32_t lastEdgeUs = 0;

EdgeStats baselineStats;
EdgeStats signalStats;
CapturePhase capturePhase = CapturePhase::Idle;
uint32_t phaseStartedMs = 0;
size_t selectedAction = 0;
uint8_t sampleCounts[kActionCount] = {};
bool storageReady = false;
bool transmitterReady = false;
uint32_t lastHeartbeatMs = 0;
rmt_channel_handle_t txChannel = nullptr;
rmt_encoder_handle_t txEncoder = nullptr;
rmt_symbol_word_t txSymbols[kCodeBits] = {};

void IRAM_ATTR onRfEdge() {
  const uint32_t now = micros();
  const uint32_t duration = now - lastEdgeUs;
  lastEdgeUs = now;
  if (duration == 0 || duration > 1000000) {
    return;
  }

  liveStats.edges += 1;
  if (duration < liveStats.minUs) {
    liveStats.minUs = duration;
  }
  if (duration > liveStats.maxUs) {
    liveStats.maxUs = duration;
  }
  if (duration < kStoredMinPulseUs) {
    liveStats.under150Us += 1;
    return;
  }
  if (duration <= 1000) {
    liveStats.from150To1000Us += 1;
  } else {
    liveStats.over1000Us += 1;
  }

  if (duration <= kStoredMaxPulseUs) {
    const size_t index = edgeRingTotal % kMaxStoredEdges;
    edgeRing[index].durationUs = static_cast<uint16_t>(duration);
    edgeRing[index].level = !digitalRead(kRfRxPin);
    edgeRingTotal += 1;
  }
}

void resetCaptureBuffer() {
  noInterrupts();
  liveStats.edges = 0;
  liveStats.minUs = UINT32_MAX;
  liveStats.maxUs = 0;
  liveStats.under150Us = 0;
  liveStats.from150To1000Us = 0;
  liveStats.over1000Us = 0;
  edgeRingTotal = 0;
  lastEdgeUs = micros();
  interrupts();
}

EdgeStats snapshotStats() {
  EdgeStats stats;
  noInterrupts();
  stats.edges = liveStats.edges;
  stats.minUs = liveStats.minUs;
  stats.maxUs = liveStats.maxUs;
  stats.under150Us = liveStats.under150Us;
  stats.from150To1000Us = liveStats.from150To1000Us;
  stats.over1000Us = liveStats.over1000Us;
  interrupts();
  if (stats.minUs == UINT32_MAX) {
    stats.minUs = 0;
  }
  return stats;
}

String samplePath(size_t actionIndex, uint8_t sampleNumber) {
  char path[48];
  snprintf(path, sizeof(path), "/%s_%02u.rfraw", kActions[actionIndex].id,
           sampleNumber);
  return String(path);
}

void scanSampleCounts() {
  for (size_t action = 0; action < kActionCount; ++action) {
    sampleCounts[action] = 0;
    for (uint8_t sample = 1; sample <= kSamplesPerAction; ++sample) {
      if (SPIFFS.exists(samplePath(action, sample))) {
        sampleCounts[action] += 1;
      }
    }
  }
}

void drawButton(int x, int y, int width, int height, const char* label,
                uint16_t color) {
  auto& display = M5.Display;
  display.fillRoundRect(x, y, width, height, 5, color);
  display.setTextDatum(middle_center);
  display.setTextColor(TFT_BLACK, color);
  display.setTextSize(2);
  display.drawString(label, x + width / 2, y + height / 2);
}

void renderMain() {
  auto& display = M5.Display;
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(top_center);
  display.setTextColor(TFT_CYAN, TFT_BLACK);
  display.setTextSize(2);
  display.drawString("CX9 RF DATASET", display.width() / 2, 8);

  drawButton(8, 50, 54, 46, "<", TFT_DARKGREY);
  drawButton(258, 50, 54, 46, ">", TFT_DARKGREY);
  display.setTextDatum(middle_center);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(2);
  display.drawString(kActions[selectedAction].label, display.width() / 2, 73);

  char progress[48];
  snprintf(progress, sizeof(progress), "Samples %u / %u",
           sampleCounts[selectedAction], kSamplesPerAction);
  display.setTextSize(1);
  display.setTextColor(sampleCounts[selectedAction] == kSamplesPerAction
                           ? TFT_GREEN
                           : TFT_ORANGE,
                       TFT_BLACK);
  display.drawString(progress, display.width() / 2, 118);
  drawButton(12, 156, 142, 66, "RECORD", TFT_GREEN);
  drawButton(166, 156, 142, 66,
             kActions[selectedAction].replayCode == 0 ? "NO CODE" : "REPLAY",
             kActions[selectedAction].replayCode == 0 ? TFT_DARKGREY : TFT_CYAN);
}

void renderCapture(const char* title, const char* detail, uint16_t color) {
  auto& display = M5.Display;
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(top_center);
  display.setTextColor(color, TFT_BLACK);
  display.setTextSize(2);
  display.drawString(title, display.width() / 2, 18);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.drawString(kActions[selectedAction].label, display.width() / 2, 62);
  display.setTextSize(1);
  display.drawString(detail, display.width() / 2, 116);
  display.drawString("Tap to cancel", display.width() / 2, 190);
}

void startCapture() {
  if (!storageReady || sampleCounts[selectedAction] >= kSamplesPerAction) {
    return;
  }
  resetCaptureBuffer();
  attachInterrupt(digitalPinToInterrupt(kRfRxPin), onRfEdge, CHANGE);
  capturePhase = CapturePhase::Baseline;
  phaseStartedMs = millis();
  renderCapture("BASELINE", "Hands off the remote", TFT_YELLOW);
  Serial.printf("RF_SAMPLE_BEGIN action=%s sample=%u\n",
                kActions[selectedAction].id,
                sampleCounts[selectedAction] + 1);
}

void cancelCapture() {
  detachInterrupt(digitalPinToInterrupt(kRfRxPin));
  capturePhase = CapturePhase::Idle;
  Serial.printf("RF_SAMPLE_CANCELED action=%s\n",
                kActions[selectedAction].id);
  renderMain();
}

bool saveCapture() {
  const uint32_t total = edgeRingTotal;
  const uint16_t count = static_cast<uint16_t>(
      min<uint32_t>(total, static_cast<uint32_t>(kMaxStoredEdges)));
  if (count < 20) {
    return false;
  }

  const uint8_t sampleNumber = sampleCounts[selectedAction] + 1;
  File file = SPIFFS.open(samplePath(selectedAction, sampleNumber), FILE_WRITE);
  if (!file) {
    return false;
  }

  CaptureHeader header = {
      .magic = kCaptureMagic,
      .version = kCaptureVersion,
      .actionIndex = static_cast<uint8_t>(selectedAction),
      .sampleNumber = sampleNumber,
      .baselineMs = kBaselineMs,
      .signalMs = kSignalMs,
      .baseline = baselineStats,
      .signal = signalStats,
      .totalStoredEligibleEdges = total,
      .storedEdgeCount = count,
      .ringOverflow = static_cast<uint8_t>(total > kMaxStoredEdges),
      .reserved = 0,
  };
  file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header));

  const size_t start = total > kMaxStoredEdges ? total % kMaxStoredEdges : 0;
  for (size_t offset = 0; offset < count; ++offset) {
    const size_t index = (start + offset) % kMaxStoredEdges;
    orderedEdges[offset] = {
        .durationUs = edgeRing[index].durationUs,
        .level = edgeRing[index].level,
    };
  }
  const size_t expectedBytes = count * sizeof(EdgeSample);
  const size_t writtenBytes = file.write(
      reinterpret_cast<const uint8_t*>(orderedEdges), expectedBytes);
  file.close();
  if (writtenBytes != expectedBytes) {
    SPIFFS.remove(samplePath(selectedAction, sampleNumber));
    return false;
  }

  sampleCounts[selectedAction] += 1;
  Serial.printf(
      "RF_SAMPLE_SAVED action=%s sample=%u edges=%lu stored=%u overflow=%s "
      "baseline_rate=%lu signal_rate=%lu path=%s\n",
      kActions[selectedAction].id, sampleNumber,
      static_cast<unsigned long>(signalStats.edges), count,
      total > kMaxStoredEdges ? "true" : "false",
      static_cast<unsigned long>(baselineStats.edges * 1000 / kBaselineMs),
      static_cast<unsigned long>(signalStats.edges * 1000 / kSignalMs),
      samplePath(selectedAction, sampleNumber).c_str());
  return true;
}

void finishCapture() {
  detachInterrupt(digitalPinToInterrupt(kRfRxPin));
  signalStats = snapshotStats();
  capturePhase = CapturePhase::Idle;
  const uint32_t totalStoredEdges = edgeRingTotal;

  auto& display = M5.Display;
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(middle_center);
  display.setTextColor(TFT_CYAN, TFT_BLACK);
  display.setTextSize(2);
  display.drawString("SAVING...", display.width() / 2, display.height() / 2);
  const bool saved = saveCapture();

  display.fillScreen(TFT_BLACK);
  display.setTextDatum(top_center);
  display.setTextColor(saved ? TFT_GREEN : TFT_RED, TFT_BLACK);
  display.setTextSize(2);
  display.drawString(saved ? "SAMPLE SAVED" : "CAPTURE FAILED",
                     display.width() / 2, 18);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  char line[80];
  snprintf(line, sizeof(line), "Baseline: %lu edges/s",
           static_cast<unsigned long>(baselineStats.edges * 1000 / kBaselineMs));
  display.drawString(line, display.width() / 2, 82);
  snprintf(line, sizeof(line), "Signal: %lu edges/s",
           static_cast<unsigned long>(signalStats.edges * 1000 / kSignalMs));
  display.drawString(line, display.width() / 2, 112);
  snprintf(line, sizeof(line), "Stored: %lu pulses",
           static_cast<unsigned long>(
               min<uint32_t>(totalStoredEdges,
                             static_cast<uint32_t>(kMaxStoredEdges))));
  display.drawString(line, display.width() / 2, 142);
  delay(1800);
  renderMain();
}

void processCapture() {
  if (capturePhase == CapturePhase::Baseline &&
      millis() - phaseStartedMs >= kBaselineMs) {
    baselineStats = snapshotStats();
    resetCaptureBuffer();
    capturePhase = CapturePhase::Signal;
    phaseStartedMs = millis();
    renderCapture("OPERATE NOW", kActions[selectedAction].instruction,
                  TFT_GREEN);
    Serial.printf("RF_SAMPLE_SIGNAL action=%s\n", kActions[selectedAction].id);
    return;
  }
  if (capturePhase == CapturePhase::Signal &&
      millis() - phaseStartedMs >= kSignalMs) {
    finishCapture();
  }
}

bool setupTransmitter() {
  rmt_tx_channel_config_t txConfig = {
      .gpio_num = kRfTxPin,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = kRmtResolutionHz,
      .mem_block_symbols = 64,
      .trans_queue_depth = 2,
      .intr_priority = 0,
      .flags = {.invert_out = false, .with_dma = false, .io_loop_back = false},
  };
  esp_err_t result = rmt_new_tx_channel(&txConfig, &txChannel);
  if (result != ESP_OK) {
    Serial.printf("RF_TX_SETUP result=channel_failed error=%d\n", result);
    return false;
  }

  rmt_copy_encoder_config_t encoderConfig = {};
  result = rmt_new_copy_encoder(&encoderConfig, &txEncoder);
  if (result == ESP_OK) {
    result = rmt_enable(txChannel);
  }
  Serial.printf("RF_TX_SETUP result=%s error=%d gpio=%d\n",
                result == ESP_OK ? "ready" : "failed", result, kRfTxPin);
  return result == ESP_OK;
}

void replaySelectedAction() {
  const Action& action = kActions[selectedAction];
  if (!transmitterReady || action.replayCode == 0) {
    Serial.printf("RF_REPLAY action=%s result=rejected reason=%s\n", action.id,
                  transmitterReady ? "no_verified_code" : "tx_not_ready");
    return;
  }

  for (uint8_t bit = 0; bit < kCodeBits; ++bit) {
    const bool one = action.replayCode & (1UL << (kCodeBits - bit - 1));
    txSymbols[bit] = {
        .duration0 = one ? kOneHighUs : kZeroHighUs,
        .level0 = 1,
        .duration1 = one ? kOneLowUs : kZeroLowUs,
        .level1 = 0,
    };
  }

  rmt_transmit_config_t transmitConfig = {
      .loop_count = 0,
      .flags = {.eot_level = 0, .queue_nonblocking = 0},
  };
  esp_err_t result = ESP_OK;
  for (uint8_t repeat = 0; repeat < kReplayRepeats && result == ESP_OK;
       ++repeat) {
    result = rmt_transmit(txChannel, txEncoder, txSymbols, sizeof(txSymbols),
                          &transmitConfig);
    if (result == ESP_OK) {
      result = rmt_tx_wait_all_done(txChannel, 1000);
    }
    if (repeat + 1 < kReplayRepeats) {
      delayMicroseconds(kFrameGapUs);
    }
  }

  Serial.printf("RF_REPLAY action=%s code=0x%06lX repeats=%u result=%s\n",
                action.id, static_cast<unsigned long>(action.replayCode),
                kReplayRepeats, result == ESP_OK ? "sent" : "tx_failed");
  auto& display = M5.Display;
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(middle_center);
  display.setTextColor(result == ESP_OK ? TFT_GREEN : TFT_RED, TFT_BLACK);
  display.setTextSize(2);
  display.drawString(result == ESP_OK ? "RF SENT" : "TX FAILED",
                     display.width() / 2, 88);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  display.drawString(action.label, display.width() / 2, 128);
  display.drawString("No device acknowledgement", display.width() / 2, 158);
  delay(900);
  renderMain();
}

void handleTouch() {
  auto touch = M5.Touch.getDetail();
  if (!touch.wasClicked()) {
    return;
  }
  Serial.printf("RF_TOUCH x=%d y=%d phase=%u\n", touch.x, touch.y,
                static_cast<unsigned>(capturePhase));
  if (capturePhase != CapturePhase::Idle) {
    cancelCapture();
    return;
  }
  if (touch.y >= 42 && touch.y <= 112) {
    if (touch.x < 90) {
      selectedAction = (selectedAction + kActionCount - 1) % kActionCount;
      renderMain();
    } else if (touch.x > 230) {
      selectedAction = (selectedAction + 1) % kActionCount;
      renderMain();
    }
    return;
  }
  if (touch.y >= 145) {
    if (touch.x < 160) {
      startCapture();
    } else {
      replaySelectedAction();
    }
  }
}

}  // namespace

void setup() {
  auto config = M5.config();
  M5.begin(config);
  Serial.begin(115200);
  delay(500);
  M5.Power.setExtOutput(true);
  pinMode(kRfRxPin, INPUT);
  transmitterReady = setupTransmitter();

  storageReady = SPIFFS.begin(true);
  if (storageReady) {
    scanSampleCounts();
  }
  renderMain();
  Serial.printf(
      "RF_DATASET_READY storage=%s actions=%u samples_per_action=%u "
      "rx_gpio=8 tx_gpio=9 tx=%s\n",
      storageReady ? "spiffs" : "failed", static_cast<unsigned>(kActionCount),
      kSamplesPerAction, transmitterReady ? "ready" : "failed");
}

void loop() {
  M5.update();
  handleTouch();
  if (capturePhase != CapturePhase::Idle) {
    processCapture();
  }
  if (millis() - lastHeartbeatMs >= 2000) {
    lastHeartbeatMs = millis();
    Serial.printf("RF_HEARTBEAT phase=%u action=%s samples=%u storage=%s\n",
                  static_cast<unsigned>(capturePhase),
                  kActions[selectedAction].id, sampleCounts[selectedAction],
                  storageReady ? "ok" : "failed");
  }
  delay(2);
}
