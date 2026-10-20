#include <Arduino.h>
#include <M5Unified.h>
#include <SPIFFS.h>
#include <driver/rmt_tx.h>

namespace {

constexpr gpio_num_t kRfTxPin = GPIO_NUM_9;
constexpr gpio_num_t kRfRxPin = GPIO_NUM_8;
constexpr uint32_t kRmtResolutionHz = 1000000;
constexpr uint32_t kCaptureMagic = 0x44593252;
constexpr uint16_t kCaptureVersion = 1;
constexpr uint32_t kProgressMagic = 0x44595052;
constexpr uint16_t kProgressVersion = 1;
constexpr uint32_t kBaselineMs = 1000;
constexpr uint32_t kSignalMs = 2500;
constexpr uint16_t kStoredMinPulseUs = 150;
constexpr uint16_t kStoredMaxPulseUs = 20000;
constexpr size_t kMaxStoredEdges = 8192;
constexpr uint8_t kSamplesPerAction = 1;
constexpr uint8_t kCurtainCount = 5;

struct Action {
  const char* id;
  const char* label;
  const char* instruction;
};

constexpr Action kCommands[] = {
    {"up", "UP", "Tap UP once"},
    {"stop", "STOP", "Tap STOP once"},
    {"down", "DOWN", "Tap DOWN once"},
};
constexpr size_t kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);

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

struct __attribute__((packed)) CaptureProgress {
  uint32_t magic;
  uint16_t version;
  uint8_t counts[kCurtainCount][kCommandCount];
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
size_t selectedCurtain = 0;
size_t selectedCommand = 0;
uint8_t sampleCounts[kCurtainCount][kCommandCount] = {};
bool storageReady = false;
bool transmitterReady = false;
uint32_t lastHeartbeatMs = 0;
const char* lastSaveError = "none";
rmt_channel_handle_t txChannel = nullptr;
rmt_encoder_handle_t txEncoder = nullptr;
rmt_symbol_word_t replaySymbols[(kMaxStoredEdges + 1) / 2] = {};

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

const Action& currentCommand() { return kCommands[selectedCommand]; }

uint8_t& currentSampleCount() {
  return sampleCounts[selectedCurtain][selectedCommand];
}

uint8_t currentActionIndex() {
  return static_cast<uint8_t>(selectedCurtain * kCommandCount + selectedCommand);
}

String latestSamplePath(size_t curtainIndex, size_t commandIndex) {
  char path[48];
  snprintf(path, sizeof(path), "/latest_curtain_%02u_%s.rfraw",
           static_cast<unsigned>(curtainIndex + 1), kCommands[commandIndex].id);
  return String(path);
}

String currentSamplePath() {
  return latestSamplePath(selectedCurtain, selectedCommand);
}

void removeArchivedLegacyCaptures() {
  constexpr const char* kLegacyActions[] = {
      "power",         "brightness_cw", "brightness_ccw", "cct_cw",
      "cct_ccw",       "preset_s1",     "preset_s2",      "preset_s3",
      "channel_plus",  "channel_minus", "up",             "stop",
      "down",
  };
  unsigned removed = 0;
  for (const char* action : kLegacyActions) {
    for (uint8_t sample = 1; sample <= 10; ++sample) {
      char path[48];
      snprintf(path, sizeof(path), "/%s_%02u.rfraw", action, sample);
      if (SPIFFS.exists(path) && SPIFFS.remove(path)) {
        removed += 1;
      }
    }
  }
  Serial.printf("RF_LEGACY_CLEANUP removed=%u used=%u total=%u\n", removed,
                static_cast<unsigned>(SPIFFS.usedBytes()),
                static_cast<unsigned>(SPIFFS.totalBytes()));
}

bool saveProgress() {
  CaptureProgress progress = {
      .magic = kProgressMagic,
      .version = kProgressVersion,
  };
  memcpy(progress.counts, sampleCounts, sizeof(sampleCounts));
  File file = SPIFFS.open("/capture_progress.bin", FILE_WRITE);
  if (!file) {
    return false;
  }
  const size_t written =
      file.write(reinterpret_cast<const uint8_t*>(&progress), sizeof(progress));
  file.flush();
  file.close();
  return written == sizeof(progress);
}

bool loadProgress() {
  File file = SPIFFS.open("/capture_progress.bin", FILE_READ);
  CaptureProgress progress = {};
  const size_t read =
      file ? file.read(reinterpret_cast<uint8_t*>(&progress), sizeof(progress)) : 0;
  if (file) {
    file.close();
  }
  if (read != sizeof(progress) || progress.magic != kProgressMagic ||
      progress.version != kProgressVersion) {
    memset(sampleCounts, 0, sizeof(sampleCounts));
    return saveProgress();
  }
  memcpy(sampleCounts, progress.counts, sizeof(sampleCounts));
  for (size_t curtain = 0; curtain < kCurtainCount; ++curtain) {
    for (size_t command = 0; command < kCommandCount; ++command) {
      sampleCounts[curtain][command] =
          min<uint8_t>(sampleCounts[curtain][command], kSamplesPerAction);
    }
  }
  return true;
}

bool isValidFrameAt(const EdgeSample* edges, size_t count, size_t sync) {
  if (sync == 0 || sync + 144 >= count || edges[sync - 1].level != 0 ||
      edges[sync - 1].durationUs < 7000 ||
      edges[sync - 1].durationUs > 14000 || edges[sync].level != 1 ||
      edges[sync].durationUs < 4500 || edges[sync].durationUs > 5100 ||
      edges[sync + 1].level != 0 || edges[sync + 1].durationUs < 1200 ||
      edges[sync + 1].durationUs > 1700) {
    return false;
  }
  for (size_t bit = 0; bit < 71; ++bit) {
    const EdgeSample& high = edges[sync + 2 + bit * 2];
    const EdgeSample& low = edges[sync + 3 + bit * 2];
    const uint16_t shortPulse = min(high.durationUs, low.durationUs);
    const uint16_t longPulse = max(high.durationUs, low.durationUs);
    if (high.level != 1 || low.level != 0 || shortPulse < 250 ||
        shortPulse > 450 || longPulse < 600 || longPulse > 850) {
      return false;
    }
  }
  const EdgeSample& terminal = edges[sync + 144];
  return terminal.level == 1 && terminal.durationUs >= 250 &&
         terminal.durationUs <= 850;
}

uint8_t countValidFrames(const EdgeSample* edges, size_t count) {
  uint8_t frames = 0;
  for (size_t sync = 1; sync + 144 < count; ++sync) {
    if (isValidFrameAt(edges, count, sync)) {
      frames += 1;
    }
  }
  return frames;
}

size_t buildCleanReplay(const EdgeSample* edges, size_t count,
                        uint8_t& frameCount) {
  constexpr size_t kSymbolsPerFrame = 73;
  constexpr size_t kReplayCapacity =
      sizeof(replaySymbols) / sizeof(replaySymbols[0]);
  size_t symbolCount = 0;
  frameCount = 0;
  for (size_t sync = 1; sync + 144 < count; ++sync) {
    if (!isValidFrameAt(edges, count, sync) ||
        symbolCount + kSymbolsPerFrame > kReplayCapacity) {
      continue;
    }

    const EdgeSample gap = edges[sync - 1];
    for (size_t pair = 0; pair < kSymbolsPerFrame; ++pair) {
      const size_t firstOffset = pair * 2;
      const size_t secondOffset = firstOffset + 1;
      const EdgeSample first =
          firstOffset <= 144 ? edges[sync + firstOffset] : gap;
      const EdgeSample second =
          secondOffset <= 144 ? edges[sync + secondOffset] : gap;
      replaySymbols[symbolCount++] = {
          .duration0 = first.durationUs,
          .level0 = first.level,
          .duration1 = second.durationUs,
          .level1 = second.level,
      };
    }
    frameCount += 1;
  }
  return symbolCount;
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
  display.drawString("DOOYA RF LEARN", display.width() / 2, 8);

  drawButton(8, 34, 48, 40, "<", TFT_DARKGREY);
  drawButton(264, 34, 48, 40, ">", TFT_DARKGREY);
  display.setTextDatum(middle_center);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(2);
  char curtainLabel[24];
  snprintf(curtainLabel, sizeof(curtainLabel), "CURTAIN %u",
           static_cast<unsigned>(selectedCurtain + 1));
  display.drawString(curtainLabel, display.width() / 2, 54);

  for (size_t command = 0; command < kCommandCount; ++command) {
    const int x = 8 + static_cast<int>(command) * 104;
    drawButton(x, 84, 96, 42, kCommands[command].label,
               command == selectedCommand ? TFT_CYAN : TFT_DARKGREY);
  }

  char progress[48];
  snprintf(progress, sizeof(progress), "Stored: %s",
           currentSampleCount() == 0 ? "none" : "latest capture");
  display.setTextSize(1);
  display.setTextColor(currentSampleCount() == kSamplesPerAction
                           ? TFT_GREEN
                           : TFT_ORANGE,
                       TFT_BLACK);
  display.drawString(progress, display.width() / 2, 140);
  drawButton(12, 156, 142, 66,
             currentSampleCount() == kSamplesPerAction ? "RE-REC" : "RECORD",
             TFT_GREEN);
  drawButton(166, 156, 142, 66,
             currentSampleCount() == 0 ? "NO SAMPLE" : "REPLAY",
             currentSampleCount() == 0 ? TFT_DARKGREY : TFT_CYAN);
}

void renderCapture(const char* title, const char* detail, uint16_t color) {
  auto& display = M5.Display;
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(top_center);
  display.setTextColor(color, TFT_BLACK);
  display.setTextSize(2);
  display.drawString(title, display.width() / 2, 18);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  char selection[32];
  snprintf(selection, sizeof(selection), "CURTAIN %u  %s",
           static_cast<unsigned>(selectedCurtain + 1), currentCommand().label);
  display.drawString(selection, display.width() / 2, 62);
  display.setTextSize(1);
  display.drawString(detail, display.width() / 2, 116);
  display.drawString("Tap to cancel", display.width() / 2, 190);
}

void startCapture() {
  if (!storageReady) {
    return;
  }
  resetCaptureBuffer();
  attachInterrupt(digitalPinToInterrupt(kRfRxPin), onRfEdge, CHANGE);
  capturePhase = CapturePhase::Baseline;
  phaseStartedMs = millis();
  renderCapture("BASELINE", "Hands off the remote", TFT_YELLOW);
  Serial.printf("RF_SAMPLE_BEGIN curtain=%u action=%s sample=%u\n",
                static_cast<unsigned>(selectedCurtain + 1),
                currentCommand().id,
                min<uint8_t>(currentSampleCount() + 1, kSamplesPerAction));
}

void cancelCapture() {
  detachInterrupt(digitalPinToInterrupt(kRfRxPin));
  capturePhase = CapturePhase::Idle;
  Serial.printf("RF_SAMPLE_CANCELED curtain=%u action=%s\n",
                static_cast<unsigned>(selectedCurtain + 1),
                currentCommand().id);
  renderMain();
}

bool saveCapture() {
  lastSaveError = "none";
  const uint32_t total = edgeRingTotal;
  const uint16_t count = static_cast<uint16_t>(
      min<uint32_t>(total, static_cast<uint32_t>(kMaxStoredEdges)));
  if (count < 20) {
    lastSaveError = "too_few_edges";
    Serial.printf(
        "RF_SAMPLE_FAILED curtain=%u action=%s reason=%s stored=%u\n",
        static_cast<unsigned>(selectedCurtain + 1), currentCommand().id,
        lastSaveError, count);
    return false;
  }

  const size_t start = total > kMaxStoredEdges ? total % kMaxStoredEdges : 0;
  for (size_t offset = 0; offset < count; ++offset) {
    const size_t index = (start + offset) % kMaxStoredEdges;
    orderedEdges[offset] = {
        .durationUs = edgeRing[index].durationUs,
        .level = edgeRing[index].level,
    };
  }
  const uint8_t validFrames = countValidFrames(orderedEdges, count);
  const uint8_t requiredFrames = selectedCommand == 1 ? 5 : 7;
  if (validFrames < requiredFrames) {
    lastSaveError = "invalid_rf";
    Serial.printf(
        "RF_SAMPLE_FAILED curtain=%u action=%s reason=%s frames=%u "
        "required=%u\n",
        static_cast<unsigned>(selectedCurtain + 1), currentCommand().id,
        lastSaveError, validFrames, requiredFrames);
    return false;
  }

  const bool replacing = currentSampleCount() >= kSamplesPerAction;
  const uint8_t sampleNumber =
      replacing ? kSamplesPerAction : currentSampleCount() + 1;
  const String path = currentSamplePath();
  File file = SPIFFS.open(path, FILE_WRITE);
  if (!file) {
    lastSaveError = "open_failed";
    Serial.printf(
        "RF_SAMPLE_FAILED curtain=%u action=%s reason=%s used=%u total=%u "
        "path=%s\n",
        static_cast<unsigned>(selectedCurtain + 1), currentCommand().id,
        lastSaveError,
        static_cast<unsigned>(SPIFFS.usedBytes()),
        static_cast<unsigned>(SPIFFS.totalBytes()),
        path.c_str());
    return false;
  }

  CaptureHeader header = {
      .magic = kCaptureMagic,
      .version = kCaptureVersion,
      .actionIndex = currentActionIndex(),
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
  const size_t headerBytes =
      file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
  if (headerBytes != sizeof(header)) {
    file.close();
    SPIFFS.remove(path);
    lastSaveError = "header_write_failed";
    Serial.printf(
        "RF_SAMPLE_FAILED curtain=%u action=%s reason=%s written=%u expected=%u "
        "used=%u total=%u\n",
        static_cast<unsigned>(selectedCurtain + 1), currentCommand().id,
        lastSaveError,
        static_cast<unsigned>(headerBytes), static_cast<unsigned>(sizeof(header)),
        static_cast<unsigned>(SPIFFS.usedBytes()),
        static_cast<unsigned>(SPIFFS.totalBytes()));
    return false;
  }

  const size_t expectedBytes = count * sizeof(EdgeSample);
  const size_t writtenBytes = file.write(
      reinterpret_cast<const uint8_t*>(orderedEdges), expectedBytes);
  file.flush();
  file.close();
  if (writtenBytes != expectedBytes) {
    SPIFFS.remove(path);
    lastSaveError = "data_write_failed";
    Serial.printf(
        "RF_SAMPLE_FAILED curtain=%u action=%s reason=%s written=%u expected=%u "
        "used=%u total=%u\n",
        static_cast<unsigned>(selectedCurtain + 1), currentCommand().id,
        lastSaveError,
        static_cast<unsigned>(writtenBytes), static_cast<unsigned>(expectedBytes),
        static_cast<unsigned>(SPIFFS.usedBytes()),
        static_cast<unsigned>(SPIFFS.totalBytes()));
    return false;
  }

  const size_t expectedFileBytes = sizeof(header) + expectedBytes;
  File savedFile = SPIFFS.open(path, FILE_READ);
  const size_t actualFileBytes = savedFile ? savedFile.size() : 0;
  if (savedFile) {
    savedFile.close();
  }
  if (actualFileBytes != expectedFileBytes) {
    SPIFFS.remove(path);
    lastSaveError = "verify_failed";
    Serial.printf(
        "RF_SAMPLE_FAILED curtain=%u action=%s reason=%s actual=%u "
        "expected=%u used=%u total=%u\n",
        static_cast<unsigned>(selectedCurtain + 1), currentCommand().id,
        lastSaveError, static_cast<unsigned>(actualFileBytes),
        static_cast<unsigned>(expectedFileBytes),
        static_cast<unsigned>(SPIFFS.usedBytes()),
        static_cast<unsigned>(SPIFFS.totalBytes()));
    return false;
  }

  if (!replacing) {
    currentSampleCount() += 1;
  }
  if (!saveProgress()) {
    if (!replacing) {
      currentSampleCount() -= 1;
    }
    lastSaveError = "progress_write_failed";
    return false;
  }
  Serial.printf(
      "RF_SAMPLE_SAVED curtain=%u action=%s sample=%u frames=%u edges=%lu "
      "stored=%u overflow=%s replace=%s baseline_rate=%lu signal_rate=%lu "
      "path=%s\n",
      static_cast<unsigned>(selectedCurtain + 1), currentCommand().id,
      sampleNumber, validFrames,
      static_cast<unsigned long>(signalStats.edges), count,
      total > kMaxStoredEdges ? "true" : "false",
      replacing ? "true" : "false",
      static_cast<unsigned long>(baselineStats.edges * 1000 / kBaselineMs),
      static_cast<unsigned long>(signalStats.edges * 1000 / kSignalMs),
      path.c_str());
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
  if (!saved) {
    display.setTextColor(TFT_RED, TFT_BLACK);
    display.drawString(lastSaveError, display.width() / 2, 52);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
  }
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
    renderCapture("OPERATE NOW", currentCommand().instruction, TFT_GREEN);
    Serial.printf("RF_SAMPLE_SIGNAL curtain=%u action=%s\n",
                  static_cast<unsigned>(selectedCurtain + 1),
                  currentCommand().id);
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
      .trans_queue_depth = 1,
      .intr_priority = 0,
      .flags = {.invert_out = false, .with_dma = false, .io_loop_back = false},
  };
  esp_err_t result = rmt_new_tx_channel(&txConfig, &txChannel);
  if (result == ESP_OK) {
    rmt_copy_encoder_config_t encoderConfig = {};
    result = rmt_new_copy_encoder(&encoderConfig, &txEncoder);
  }
  if (result == ESP_OK) {
    result = rmt_enable(txChannel);
  }
  Serial.printf("RF_TX_SETUP result=%s error=%d gpio=%d\n",
                result == ESP_OK ? "ready" : "failed", result, kRfTxPin);
  return result == ESP_OK;
}

void replayLatestCapture() {
  if (!transmitterReady || currentSampleCount() == 0) {
    return;
  }

  const String path = currentSamplePath();
  File file = SPIFFS.open(path, FILE_READ);
  CaptureHeader header = {};
  const size_t headerBytes =
      file ? file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) : 0;
  if (!file || headerBytes != sizeof(header) || header.magic != kCaptureMagic ||
      header.version != kCaptureVersion || header.storedEdgeCount < 2 ||
      header.storedEdgeCount > kMaxStoredEdges) {
    if (file) {
      file.close();
    }
    Serial.printf(
        "RF_RAW_REPLAY curtain=%u action=%s result=invalid_capture path=%s\n",
        static_cast<unsigned>(selectedCurtain + 1), currentCommand().id,
        path.c_str());
    return;
  }

  const size_t edgeBytes = header.storedEdgeCount * sizeof(EdgeSample);
  const size_t readBytes =
      file.read(reinterpret_cast<uint8_t*>(orderedEdges), edgeBytes);
  file.close();
  if (readBytes != edgeBytes) {
    Serial.printf(
        "RF_RAW_REPLAY curtain=%u action=%s result=short_read read=%u "
        "expected=%u\n",
        static_cast<unsigned>(selectedCurtain + 1), currentCommand().id,
        static_cast<unsigned>(readBytes),
        static_cast<unsigned>(edgeBytes));
    return;
  }

  uint8_t frameCount = 0;
  const size_t symbolCount =
      buildCleanReplay(orderedEdges, header.storedEdgeCount, frameCount);
  if (symbolCount == 0) {
    Serial.printf(
        "RF_CLEAN_REPLAY curtain=%u action=%s result=no_valid_frames path=%s\n",
        static_cast<unsigned>(selectedCurtain + 1), currentCommand().id,
        path.c_str());
    return;
  }

  rmt_transmit_config_t transmitConfig = {
      .loop_count = 0,
      .flags = {.eot_level = 0, .queue_nonblocking = 0},
  };
  esp_err_t result = rmt_transmit(
      txChannel, txEncoder, replaySymbols,
      symbolCount * sizeof(rmt_symbol_word_t), &transmitConfig);
  if (result == ESP_OK) {
    result = rmt_tx_wait_all_done(txChannel, 5000);
  }

  Serial.printf(
      "RF_CLEAN_REPLAY curtain=%u action=%s sample=%u frames=%u symbols=%u "
      "result=%s path=%s\n",
      static_cast<unsigned>(selectedCurtain + 1), currentCommand().id,
      currentSampleCount(), frameCount, static_cast<unsigned>(symbolCount),
      result == ESP_OK ? "sent" : "tx_failed", path.c_str());

  auto& display = M5.Display;
  display.fillScreen(TFT_BLACK);
  display.setTextDatum(middle_center);
  display.setTextColor(result == ESP_OK ? TFT_GREEN : TFT_RED, TFT_BLACK);
  display.setTextSize(2);
  display.drawString(result == ESP_OK ? "CLEAN SENT" : "TX FAILED",
                     display.width() / 2, 82);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(1);
  char selection[32];
  snprintf(selection, sizeof(selection), "CURTAIN %u  %s",
           static_cast<unsigned>(selectedCurtain + 1), currentCommand().label);
  display.drawString(selection, display.width() / 2, 122);
  display.drawString("No curtain acknowledgement", display.width() / 2, 152);
  delay(1400);
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
  if (touch.y >= 30 && touch.y <= 78) {
    if (touch.x < 80) {
      selectedCurtain =
          (selectedCurtain + kCurtainCount - 1) % kCurtainCount;
      renderMain();
    } else if (touch.x > 240) {
      selectedCurtain = (selectedCurtain + 1) % kCurtainCount;
      renderMain();
    }
    return;
  }
  if (touch.y >= 82 && touch.y <= 132) {
    selectedCommand = min<size_t>(touch.x / 104, kCommandCount - 1);
    renderMain();
    return;
  }
  if (touch.y >= 150) {
    if (touch.x < 160) {
      startCapture();
    } else {
      replayLatestCapture();
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
    removeArchivedLegacyCaptures();
    storageReady = loadProgress();
  }
  renderMain();
  Serial.printf(
      "RF_DATASET_READY storage=%s curtains=%u commands=%u "
      "samples_per_action=%u "
      "rx_gpio=8 capture_magic=0x%08lX used=%u total=%u\n",
      storageReady ? "spiffs" : "failed", static_cast<unsigned>(kCurtainCount),
      static_cast<unsigned>(kCommandCount), kSamplesPerAction,
      static_cast<unsigned long>(kCaptureMagic),
      storageReady ? static_cast<unsigned>(SPIFFS.usedBytes()) : 0,
      storageReady ? static_cast<unsigned>(SPIFFS.totalBytes()) : 0);
}

void loop() {
  M5.update();
  handleTouch();
  if (capturePhase != CapturePhase::Idle) {
    processCapture();
  }
  if (millis() - lastHeartbeatMs >= 2000) {
    lastHeartbeatMs = millis();
    Serial.printf(
        "RF_HEARTBEAT phase=%u curtain=%u action=%s samples=%u storage=%s "
        "used=%u total=%u tx=%s\n",
        static_cast<unsigned>(capturePhase),
        static_cast<unsigned>(selectedCurtain + 1), currentCommand().id,
        currentSampleCount(), storageReady ? "ok" : "failed",
        storageReady ? static_cast<unsigned>(SPIFFS.usedBytes()) : 0,
        storageReady ? static_cast<unsigned>(SPIFFS.totalBytes()) : 0,
        transmitterReady ? "ready" : "failed");
  }
  delay(2);
}
