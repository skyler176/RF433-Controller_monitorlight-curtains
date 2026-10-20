#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoMqttClient.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <driver/rmt_encoder.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>

#if __has_include("config.local.h")
#include "config.local.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define MQTT_BROKER_HOST ""
#define MQTT_BROKER_PORT 1883
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define DEVICE_ID "humanex-cx9-ba-sticks3"
#define MQTT_CLIENT_ID "humanex-cx9-ba-sticks3"
#define MQTT_COMMAND_TOPIC "humanex-monitorlight/ir/cx9-ba/command"
#define MQTT_STATE_TOPIC "humanex-monitorlight/ir/cx9-ba/state"
#define MQTT_EVENT_TOPIC "humanex-monitorlight/ir/cx9-ba/event"
#define MQTT_PRESENCE_TOPIC "humanex-monitorlight/ir/cx9-ba/presence"
#endif

namespace {

constexpr char kFirmwareVersion[] = "0.1.0";
constexpr gpio_num_t kIrTxPin = GPIO_NUM_46;
constexpr gpio_num_t kIrRxPin = GPIO_NUM_42;
constexpr uint32_t kRmtResolutionHz = 1000000;
constexpr uint32_t kIrCarrierHz = 38000;
constexpr float kIrDutyCycle = 0.33f;
constexpr size_t kMaxSymbols = 256;
constexpr uint32_t kStoredCodeMagic = 0x49524331;
constexpr unsigned long kLongPressMs = 900;
constexpr unsigned long kLearningTimeoutMs = 15000;
constexpr unsigned long kMqttPresenceIntervalMs = 15000;
constexpr unsigned long kWifiRetryMs = 10000;
constexpr unsigned long kMqttReconnectMinMs = 500;
constexpr unsigned long kMqttReconnectMaxMs = 8000;

struct CommandSlot {
  const char* id;
  const char* label;
  const char* storageKey;
};

constexpr CommandSlot kCommands[] = {
    {"power_on", "POWER ON", "p_on"},
    {"power_off", "POWER OFF", "p_off"},
    {"brightness_up", "BRIGHT +", "b_up"},
    {"brightness_down", "BRIGHT -", "b_down"},
    {"warm", "WARM", "warm"},
    {"neutral", "NEUTRAL", "neutral"},
    {"cool", "COOL", "cool"},
    {"color_temp_up", "CCT +", "cct_up"},
    {"color_temp_down", "CCT -", "cct_down"},
};

constexpr size_t kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);

struct StoredPulse {
  uint16_t markUs;
  uint16_t spaceUs;
};

struct StoredCode {
  uint32_t magic = kStoredCodeMagic;
  uint16_t symbolCount = 0;
  StoredPulse pulses[kMaxSymbols] = {};
};

struct ButtonTracker {
  bool previous = false;
  unsigned long pressedAt = 0;
};

Preferences preferences;
StoredCode learnedCodes[kCommandCount];
bool learned[kCommandCount] = {};
size_t selectedIndex = 0;
bool learningMode = false;
unsigned long learningStartedMs = 0;
String lastAction = "boot";
String lastCommand = "";
String lastResult = "ready";

rmt_channel_handle_t rxChannel = nullptr;
rmt_channel_handle_t txChannel = nullptr;
rmt_encoder_handle_t txEncoder = nullptr;
rmt_symbol_word_t rxSymbols[kMaxSymbols] = {};
rmt_symbol_word_t txSymbols[kMaxSymbols] = {};
volatile bool rxDone = false;
volatile size_t rxSymbolCount = 0;

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
bool mqttConfigured = false;
bool mqttSubscribed = false;
unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long mqttReconnectDelayMs = kMqttReconnectMinMs;
unsigned long lastPresenceMs = 0;

ButtonTracker buttonA;
ButtonTracker buttonB;

void publishState();

const CommandSlot& selectedCommand() {
  return kCommands[selectedIndex];
}

int commandIndex(const String& id) {
  for (size_t index = 0; index < kCommandCount; ++index) {
    if (id == kCommands[index].id) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

void renderScreen() {
  auto& display = M5.Display;
  const int width = display.width();
  const int height = display.height();

  display.fillScreen(TFT_BLACK);
  display.setTextDatum(top_center);
  display.setTextColor(learningMode ? TFT_YELLOW : TFT_CYAN, TFT_BLACK);
  display.setTextSize(2);
  display.drawString(learningMode ? "LEARN" : "IR REMOTE", width / 2, 6);

  display.setTextColor(TFT_WHITE, TFT_BLACK);
  display.setTextSize(2);
  display.drawString(selectedCommand().label, width / 2, 38);

  display.setTextSize(1);
  display.setTextColor(learned[selectedIndex] ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  display.drawString(learned[selectedIndex] ? "SAVED" : "NOT LEARNED", width / 2, 68);

  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  if (learningMode) {
    display.drawString("Press original remote", width / 2, 90);
  } else {
    display.drawString("A next / hold learn", width / 2, 88);
    display.drawString("B send", width / 2, 103);
  }

  display.setTextDatum(top_left);
  display.setTextColor(mqttClient.connected() ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
  display.drawString(mqttClient.connected() ? "MQTT" : "LOCAL", 4, height - 14);
}

void printCodeJson(size_t index) {
  const StoredCode& code = learnedCodes[index];
  Serial.printf("IR_CODE_JSON_BEGIN %s\n", kCommands[index].id);
  Serial.printf("{\"command\":\"%s\",\"carrier_hz\":%lu,\"symbols\":[",
                kCommands[index].id, static_cast<unsigned long>(kIrCarrierHz));
  for (size_t i = 0; i < code.symbolCount; ++i) {
    if (i > 0) {
      Serial.print(',');
    }
    Serial.printf("[%u,%u]", code.pulses[i].markUs, code.pulses[i].spaceUs);
  }
  Serial.println("]}");
  Serial.printf("IR_CODE_JSON_END %s\n", kCommands[index].id);
}

void loadCodes() {
  preferences.begin("humanex-ir", false);
  for (size_t index = 0; index < kCommandCount; ++index) {
    if (!preferences.isKey(kCommands[index].storageKey)) {
      continue;
    }
    StoredCode code;
    const size_t bytes = preferences.getBytes(kCommands[index].storageKey, &code, sizeof(code));
    if (bytes == sizeof(code) && code.magic == kStoredCodeMagic &&
        code.symbolCount > 0 && code.symbolCount <= kMaxSymbols) {
      learnedCodes[index] = code;
      learned[index] = true;
      Serial.printf("Loaded command=%s symbols=%u\n", kCommands[index].id, code.symbolCount);
    }
  }
}

bool saveCode(size_t index, const rmt_symbol_word_t* symbols, size_t count) {
  if (count == 0 || count > kMaxSymbols) {
    return false;
  }

  StoredCode code;
  code.symbolCount = static_cast<uint16_t>(count);
  for (size_t i = 0; i < count; ++i) {
    code.pulses[i].markUs = static_cast<uint16_t>(symbols[i].duration0);
    code.pulses[i].spaceUs = static_cast<uint16_t>(symbols[i].duration1);
  }

  if (preferences.putBytes(kCommands[index].storageKey, &code, sizeof(code)) != sizeof(code)) {
    return false;
  }

  learnedCodes[index] = code;
  learned[index] = true;
  printCodeJson(index);
  return true;
}

bool onRmtReceiveDone(rmt_channel_handle_t, const rmt_rx_done_event_data_t* event,
                      void*) {
  rxSymbolCount = event->num_symbols;
  rxDone = true;
  return true;
}

void setupRmt() {
  rmt_rx_channel_config_t rxConfig = {
      .gpio_num = kIrRxPin,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = kRmtResolutionHz,
      .mem_block_symbols = 128,
      .intr_priority = 0,
      .flags = {.invert_in = false, .with_dma = false, .io_loop_back = false},
  };
  ESP_ERROR_CHECK(rmt_new_rx_channel(&rxConfig, &rxChannel));

  rmt_rx_event_callbacks_t callbacks = {.on_recv_done = onRmtReceiveDone};
  ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rxChannel, &callbacks, nullptr));
  ESP_ERROR_CHECK(rmt_enable(rxChannel));

  rmt_tx_channel_config_t txConfig = {
      .gpio_num = kIrTxPin,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = kRmtResolutionHz,
      .mem_block_symbols = 64,
      .trans_queue_depth = 4,
      .intr_priority = 0,
      .flags = {.invert_out = false, .with_dma = false, .io_loop_back = false},
  };
  ESP_ERROR_CHECK(rmt_new_tx_channel(&txConfig, &txChannel));

  rmt_carrier_config_t carrierConfig = {
      .frequency_hz = kIrCarrierHz,
      .duty_cycle = kIrDutyCycle,
      .flags = {.polarity_active_low = false, .always_on = false},
  };
  ESP_ERROR_CHECK(rmt_apply_carrier(txChannel, &carrierConfig));

  rmt_copy_encoder_config_t encoderConfig = {};
  ESP_ERROR_CHECK(rmt_new_copy_encoder(&encoderConfig, &txEncoder));
  ESP_ERROR_CHECK(rmt_enable(txChannel));
}

bool armReceiver() {
  rxDone = false;
  rxSymbolCount = 0;
  rmt_receive_config_t receiveConfig = {
      .signal_range_min_ns = 1000,
      .signal_range_max_ns = 20000000,
      .flags = {.en_partial_rx = false},
  };
  esp_err_t result =
      rmt_receive(rxChannel, rxSymbols, sizeof(rxSymbols), &receiveConfig);
  if (result != ESP_OK) {
    Serial.printf("IR receive arm failed error=%s\n", esp_err_to_name(result));
    return false;
  }
  return true;
}

void stopLearning(const char* result) {
  esp_err_t disableResult = rmt_disable(rxChannel);
  if (disableResult == ESP_OK) {
    ESP_ERROR_CHECK(rmt_enable(rxChannel));
  }
  learningMode = false;
  rxDone = false;
  rxSymbolCount = 0;
  lastAction = "learn";
  lastCommand = selectedCommand().id;
  lastResult = result;
  Serial.printf("IR learning stopped command=%s result=%s\n", selectedCommand().id,
                result);
  publishState();
  renderScreen();
}

void printCapturedSymbols(size_t count) {
  Serial.printf("IR capture command=%s symbols=%u\n", selectedCommand().id,
                static_cast<unsigned>(count));
  for (size_t index = 0; index < count; ++index) {
    Serial.printf("  symbol[%u] level0=%u duration0=%u level1=%u duration1=%u\n",
                  static_cast<unsigned>(index), rxSymbols[index].level0,
                  rxSymbols[index].duration0, rxSymbols[index].level1,
                  rxSymbols[index].duration1);
  }
}

void startLearning() {
  learningMode = true;
  learningStartedMs = millis();
  lastAction = "learning";
  lastCommand = selectedCommand().id;
  lastResult = "waiting_for_remote";

  if (!armReceiver()) {
    learningMode = false;
    lastResult = "rx_start_failed";
  } else {
    Serial.printf("IR learning command=%s\n", selectedCommand().id);
  }
  renderScreen();
}

bool sendLearnedCode(size_t index, const char* source) {
  if (!learned[index]) {
    lastAction = "send";
    lastCommand = kCommands[index].id;
    lastResult = "not_learned";
    Serial.printf("IR send rejected command=%s reason=not_learned\n", kCommands[index].id);
    return false;
  }

  const StoredCode& code = learnedCodes[index];
  for (size_t i = 0; i < code.symbolCount; ++i) {
    txSymbols[i] = {
        .duration0 = code.pulses[i].markUs,
        .level0 = 1,
        .duration1 = code.pulses[i].spaceUs,
        .level1 = 0,
    };
  }

  rmt_transmit_config_t transmitConfig = {
      .loop_count = 0,
      .flags = {.eot_level = 0, .queue_nonblocking = 0},
  };
  esp_err_t result = rmt_transmit(txChannel, txEncoder, txSymbols,
                                  code.symbolCount * sizeof(rmt_symbol_word_t),
                                  &transmitConfig);
  if (result == ESP_OK) {
    result = rmt_tx_wait_all_done(txChannel, 1000);
  }

  lastAction = "send";
  lastCommand = kCommands[index].id;
  lastResult = result == ESP_OK ? "sent" : "tx_failed";
  Serial.printf("IR send command=%s source=%s result=%s symbols=%u\n",
                kCommands[index].id, source, lastResult.c_str(), code.symbolCount);
  renderScreen();
  return result == ESP_OK;
}

String buildPresencePayload(bool online) {
  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["firmware_version"] = kFirmwareVersion;
  doc["status"] = online ? "online" : "offline";
  if (online && WiFi.status() == WL_CONNECTED) {
    doc["ip_address"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
  }
  String body;
  serializeJson(doc, body);
  return body;
}

bool publishJson(const char* topic, const String& body, bool retain, int qos) {
  if (!mqttClient.connected()) {
    return false;
  }
  if (!mqttClient.beginMessage(topic, body.length(), retain, qos)) {
    return false;
  }
  mqttClient.print(body);
  return mqttClient.endMessage();
}

void publishPresence(bool online) {
  if (publishJson(MQTT_PRESENCE_TOPIC, buildPresencePayload(online), true, 0)) {
    lastPresenceMs = millis();
  }
}

void publishState() {
  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["selected_command"] = selectedCommand().id;
  doc["learning"] = learningMode;
  doc["last_action"] = lastAction;
  doc["last_command"] = lastCommand;
  doc["last_result"] = lastResult;
  doc["state_is_assumed"] = true;
  JsonObject learnedObject = doc["learned"].to<JsonObject>();
  for (size_t index = 0; index < kCommandCount; ++index) {
    learnedObject[kCommands[index].id] = learned[index];
  }
  String body;
  serializeJson(doc, body);
  publishJson(MQTT_STATE_TOPIC, body, true, 1);
}

void publishEvent(const char* type, const char* command, const char* result,
                  const char* source, size_t symbolCount) {
  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["type"] = type;
  doc["command"] = command;
  doc["result"] = result;
  doc["source"] = source;
  doc["symbol_count"] = symbolCount;
  doc["hardware_confirmed"] = false;
  String body;
  serializeJson(doc, body);
  publishJson(MQTT_EVENT_TOPIC, body, false, 1);
}

void configureMqtt() {
  mqttClient.setId(MQTT_CLIENT_ID);
  if (String(MQTT_USERNAME).length() > 0) {
    mqttClient.setUsernamePassword(MQTT_USERNAME, MQTT_PASSWORD);
  }
  mqttClient.setConnectionTimeout(3000);
  mqttClient.setKeepAliveInterval(30000);

  String will = buildPresencePayload(false);
  mqttClient.beginWill(MQTT_PRESENCE_TOPIC, will.length(), true, 0);
  mqttClient.print(will);
  mqttClient.endWill();
  mqttConfigured = true;
}

void handleMqttCommand(const String& body) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error || !doc["command"].is<const char*>()) {
    lastAction = "send";
    lastCommand = "";
    lastResult = "invalid_command_json";
    publishEvent("error", "", lastResult.c_str(), "mqtt", 0);
    publishState();
    return;
  }

  String command = doc["command"].as<String>();
  int index = commandIndex(command);
  if (index < 0) {
    lastAction = "send";
    lastCommand = command;
    lastResult = "unknown_command";
    publishEvent("error", command.c_str(), lastResult.c_str(), "mqtt", 0);
    publishState();
    return;
  }

  bool sent = sendLearnedCode(static_cast<size_t>(index), "mqtt");
  publishEvent("send", command.c_str(), lastResult.c_str(), "mqtt",
               sent ? learnedCodes[index].symbolCount : 0);
  publishState();
}

void pollMqttMessage() {
  int messageSize = mqttClient.parseMessage();
  if (messageSize <= 0) {
    return;
  }
  String topic = mqttClient.messageTopic();
  String body;
  while (mqttClient.available()) {
    body += static_cast<char>(mqttClient.read());
  }
  Serial.printf("MQTT received topic=%s bytes=%d\n", topic.c_str(), messageSize);
  if (topic == MQTT_COMMAND_TOPIC) {
    handleMqttCommand(body);
  }
}

void maintainNetwork() {
  if (String(WIFI_SSID).length() == 0 || String(MQTT_BROKER_HOST).length() == 0) {
    return;
  }

  unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiAttemptMs >= kWifiRetryMs || lastWifiAttemptMs == 0) {
      lastWifiAttemptMs = now;
      mqttClient.stop();
      mqttSubscribed = false;
      Serial.printf("Wi-Fi connecting ssid=%s\n", WIFI_SSID);
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    return;
  }

  if (mqttClient.connected()) {
    if (!mqttSubscribed) {
      mqttSubscribed = mqttClient.subscribe(MQTT_COMMAND_TOPIC, 1);
      Serial.printf("MQTT subscribe topic=%s result=%d\n", MQTT_COMMAND_TOPIC,
                    mqttSubscribed ? 1 : 0);
      if (mqttSubscribed) {
        publishPresence(true);
        publishState();
        renderScreen();
      }
    }
    pollMqttMessage();
    if (now - lastPresenceMs >= kMqttPresenceIntervalMs) {
      publishPresence(true);
    }
    return;
  }

  mqttSubscribed = false;
  if (now - lastMqttAttemptMs < mqttReconnectDelayMs && lastMqttAttemptMs != 0) {
    return;
  }
  lastMqttAttemptMs = now;
  if (!mqttConfigured) {
    configureMqtt();
  }

  Serial.printf("MQTT connecting broker=%s:%d\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  if (mqttClient.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT)) {
    mqttReconnectDelayMs = kMqttReconnectMinMs;
    lastPresenceMs = 0;
    Serial.println("MQTT connected");
  } else {
    Serial.printf("MQTT connect failed error=%d\n", mqttClient.connectError());
    mqttClient.stop();
    mqttReconnectDelayMs =
        min(mqttReconnectDelayMs * 2, kMqttReconnectMaxMs);
  }
}

void finishLearningIfReady() {
  if (!learningMode || !rxDone) {
    return;
  }

  rxDone = false;
  const size_t count = min(static_cast<size_t>(rxSymbolCount), kMaxSymbols);
  lastAction = "learn";
  lastCommand = selectedCommand().id;
  printCapturedSymbols(count);

  if (count < 4) {
    lastResult = "signal_too_short_retry";
    Serial.printf("IR learn retry command=%s reason=signal_too_short\n",
                  selectedCommand().id);
    if (!armReceiver()) {
      stopLearning("rx_restart_failed");
      return;
    }
    publishState();
    renderScreen();
    return;
  } else if (saveCode(selectedIndex, rxSymbols, count)) {
    learningMode = false;
    lastResult = "saved";
    Serial.printf("IR learned command=%s symbols=%u\n", selectedCommand().id,
                  static_cast<unsigned>(count));
  } else {
    learningMode = false;
    lastResult = "save_failed";
  }

  publishEvent("learn", selectedCommand().id, lastResult.c_str(), "local", count);
  publishState();
  renderScreen();
}

void handleButton(ButtonTracker& tracker, bool pressed, bool isA) {
  unsigned long now = millis();
  if (pressed && !tracker.previous) {
    tracker.pressedAt = now;
  }

  if (!pressed && tracker.previous) {
    unsigned long duration = now - tracker.pressedAt;
    if (isA && duration >= kLongPressMs) {
      if (!learningMode) {
        startLearning();
        publishState();
      }
    } else if (isA) {
      if (!learningMode) {
        selectedIndex = (selectedIndex + 1) % kCommandCount;
        lastAction = "select";
        lastCommand = selectedCommand().id;
        lastResult = learned[selectedIndex] ? "learned" : "not_learned";
        publishState();
        renderScreen();
      }
    } else if (learningMode) {
      stopLearning("cancelled");
    } else {
      bool sent = sendLearnedCode(selectedIndex, "button");
      publishEvent("send", selectedCommand().id, lastResult.c_str(), "button",
                   sent ? learnedCodes[selectedIndex].symbolCount : 0);
      publishState();
    }
  }
  tracker.previous = pressed;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  auto config = M5.config();
  config.internal_mic = false;
  config.internal_spk = false;
  M5.begin(config);
  M5.Speaker.end();
  M5.Power.setExtOutput(true, m5::ext_none);

  M5.Display.setRotation(3);
  M5.Display.setTextWrap(false);

  loadCodes();
  setupRmt();
  renderScreen();

  WiFi.mode(WIFI_STA);
  Serial.printf("StickS3 IR MQTT Remote firmware=%s device=%s\n", kFirmwareVersion,
                DEVICE_ID);
  Serial.println("A short=next, A hold=learn, B short=send");
}

void loop() {
  M5.update();
  handleButton(buttonA, M5.BtnA.isPressed(), true);
  handleButton(buttonB, M5.BtnB.isPressed(), false);
  finishLearningIfReady();
  if (learningMode && millis() - learningStartedMs >= kLearningTimeoutMs) {
    stopLearning("timeout");
  }
  maintainNetwork();
  delay(5);
}
