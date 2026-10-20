#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <M5Unified.h>
#include <WebServer.h>
#include <WiFi.h>
#include <driver/rmt_tx.h>
#include <zenoh-pico.h>

#include <atomic>

#include "curtain_page.h"
#include "curtain_rf_codes.h"

#if __has_include("network_config.h")
#include "network_config.h"
#else
#define CX9_WIFI_SSID ""
#define CX9_WIFI_PASSWORD ""
#define CX9_ZENOH_CONNECT ""
#define CX9_DEVICE_ID "cx9-monitor-light"
#endif

namespace {

constexpr char kFirmwareVersion[] = "0.3.3";
constexpr char kProtocolVersion[] = "cx9-rf-zenoh/1";
constexpr char kCurtainHostname[] = "office-curtains";
constexpr gpio_num_t kRfTxPin = GPIO_NUM_9;
constexpr uint32_t kRmtResolutionHz = 1000000;
constexpr uint16_t kZeroHighUs = 173;
constexpr uint16_t kZeroLowUs = 547;
constexpr uint16_t kOneHighUs = 513;
constexpr uint16_t kOneLowUs = 202;
constexpr uint16_t kFrameGapUs = 3149;
constexpr uint8_t kCodeBits = 24;
constexpr uint8_t kReplayRepeats = 5;
constexpr uint32_t kSelectLongPressMs = 650;
constexpr uint32_t kHoldStartMs = 500;
constexpr uint32_t kHoldRepeatIntervalMs = 300;
constexpr uint32_t kStatusDisplayMs = 750;
constexpr uint32_t kNetworkRetryMs = 5000;
constexpr uint32_t kZenohInboundTimeoutMs = 30000;
constexpr uint8_t kMaxRemoteRepeat = 10;
constexpr size_t kMaxTxSymbols = 584;
constexpr uint8_t kCurtainQueueDepth = 8;

enum class UiScreen { Home, MonitorLight, CurtainSelect, CurtainAction };

struct Command {
  const char* id;
  const char* label;
  uint32_t code;
  bool repeatable;
};

constexpr Command kCommands[] = {
    {"power_toggle", "POWER", 0x162A5E, false},
    {"brightness_cw", "BRIGHT +", 0x162AAA, true},
    {"brightness_ccw", "BRIGHT -", 0x162A5D, true},
    {"cct_cw", "CCT CW", 0x162AAF, true},
    {"cct_ccw", "CCT CCW", 0x162A5F, true},
    {"preset_s1", "PRESET S1", 0x162AC5, false},
    {"preset_s2", "PRESET S2", 0x162A5B, false},
    {"preset_s3", "PRESET S3", 0x162AA5, false},
};
constexpr size_t kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);

struct ButtonTracker {
  bool wasPressed = false;
  bool holdTriggered = false;
  uint32_t pressedAtMs = 0;
  uint32_t lastRepeatMs = 0;
};

struct CurtainRequest {
  uint8_t curtain;
  uint8_t action;
};

size_t selectedIndex = 0;
UiScreen uiScreen = UiScreen::Home;
uint8_t selectedCurtain = 0;
uint8_t selectedCurtainAction = 0;
bool transmitterReady = false;
std::atomic<bool> statusVisible{false};
std::atomic<bool> lastTransmitSucceeded{false};
std::atomic<uint32_t> statusShownAtMs{0};
std::atomic<bool> screenDirty{true};
ButtonTracker buttonA;
ButtonTracker buttonB;
rmt_channel_handle_t txChannel = nullptr;
rmt_encoder_handle_t txEncoder = nullptr;
rmt_symbol_word_t txSymbols[kMaxTxSymbols] = {};
SemaphoreHandle_t txMutex = nullptr;
QueueHandle_t curtainQueue = nullptr;
WebServer curtainServer(80);
std::atomic<bool> curtainHttpStarted{false};
bool curtainMdnsStarted = false;

z_owned_session_t zenohSession;
z_owned_queryable_t commandQueryable;
z_owned_queryable_t infoQueryable;
z_owned_liveliness_token_t liveToken;
std::atomic<bool> zenohConnected{false};
std::atomic<uint32_t> lastZenohInboundAtMs{0};
std::atomic<uint32_t> zenohSessionOpenCount{0};
std::atomic<uint32_t> zenohInboundTimeoutCount{0};
bool commandQueryableDeclared = false;
bool infoQueryableDeclared = false;
bool liveTokenDeclared = false;
uint32_t lastNetworkAttemptAtMs = 0;
String keyPrefix;
String commandKey;
String infoKey;
String eventKey;
String liveKey;

const Command& selectedCommand() { return kCommands[selectedIndex]; }

const char* curtainActionLabel(uint8_t action) {
  constexpr const char* kLabels[] = {"UP", "STOP", "DOWN"};
  return kLabels[min<uint8_t>(action, 2)];
}

curtain_rf_data::Action curtainAction(uint8_t action) {
  constexpr curtain_rf_data::Action kActions[] = {
      curtain_rf_data::Action::Up,
      curtain_rf_data::Action::Stop,
      curtain_rf_data::Action::Down,
  };
  return kActions[min<uint8_t>(action, 2)];
}

const curtain_rf_data::Waveform* findCurtainWaveform(uint8_t curtain,
                                                     uint8_t action) {
  for (const auto& waveform : curtain_rf_data::kWaveforms) {
    if (waveform.curtain == curtain &&
        waveform.action == curtainAction(action)) {
      return &waveform;
    }
  }
  return nullptr;
}

const Command* findCommand(const char* id) {
  for (const auto& command : kCommands) {
    if (strcmp(command.id, id) == 0) {
      return &command;
    }
  }
  return nullptr;
}

void renderScreen() {
  auto& display = M5.Display;
  const int width = display.width();
  const int height = display.height();

  display.fillScreen(TFT_BLACK);
  display.setTextDatum(top_center);
  display.setTextColor(TFT_CYAN, TFT_BLACK);
  display.setTextSize(2);
  display.drawString(uiScreen == UiScreen::Home ? "DEVICE" :
                         uiScreen == UiScreen::MonitorLight ? "MONITOR LIGHT" :
                                                             "CURTAINS",
                     width / 2, 5);

  display.setTextColor(TFT_WHITE, TFT_BLACK);
  if (uiScreen == UiScreen::Home) {
    display.setTextSize(2);
    display.drawString("A  LIGHT", width / 2, 38);
    display.drawString("B  CURTAINS", width / 2, 66);
  } else {
    display.setTextSize(2);
    if (uiScreen == UiScreen::MonitorLight) {
      display.drawString(selectedCommand().label, width / 2, 39);
    } else {
      char label[32];
      if (uiScreen == UiScreen::CurtainSelect) {
        snprintf(label, sizeof(label), "CURTAIN %u", selectedCurtain + 1);
      } else {
        snprintf(label, sizeof(label), "C%u  %s", selectedCurtain + 1,
                 curtainActionLabel(selectedCurtainAction));
      }
      display.drawString(label, width / 2, 39);
    }

    display.setTextSize(1);
    if (statusVisible.load()) {
      display.setTextColor(lastTransmitSucceeded.load() ? TFT_GREEN : TFT_RED,
                           TFT_BLACK);
      display.drawString(lastTransmitSucceeded.load() ? "SENT" : "TX FAILED",
                         width / 2, 72);
    } else {
      display.setTextColor(transmitterReady ? TFT_GREEN : TFT_RED, TFT_BLACK);
      display.drawString(transmitterReady ? "READY" : "TX NOT READY", width / 2,
                         72);
    }
  }

  display.setTextSize(1);
  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  if (uiScreen != UiScreen::Home) {
    display.setTextDatum(bottom_left);
    display.drawString("A NEXT/HOLD BACK", 5, height - 5);
    display.setTextDatum(bottom_right);
    if (uiScreen == UiScreen::MonitorLight) {
      display.drawString(selectedCommand().repeatable ? "B SEND/HOLD" : "B SEND",
                         width - 5, height - 5);
    } else {
      display.drawString(uiScreen == UiScreen::CurtainSelect ? "B ENTER" : "B SEND",
                         width - 5, height - 5);
    }
  }

  display.setTextDatum(top_right);
  const bool networkReady =
      zenohConnected.load() && WiFi.status() == WL_CONNECTED;
  display.setTextColor(networkReady ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
  display.drawString(networkReady ? "Z" :
                         WiFi.status() == WL_CONNECTED ? "W" : "-",
                     width - 4, 5);
  screenDirty.store(false);
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

bool transmit(const Command& command, const char* source,
              uint8_t commandRepeats = 1) {
  if (!transmitterReady) {
    Serial.printf("RF_REPLAY action=%s source=%s result=tx_not_ready\n", command.id,
                  source);
    return false;
  }

  if (xSemaphoreTake(txMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
    Serial.printf("RF_REPLAY action=%s source=%s result=busy\n", command.id,
                  source);
    return false;
  }

  for (uint8_t bit = 0; bit < kCodeBits; ++bit) {
    const bool one = command.code & (1UL << (kCodeBits - bit - 1));
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
  for (uint8_t commandRepeat = 0;
      commandRepeat < commandRepeats && result == ESP_OK; ++commandRepeat) {
    for (uint8_t frame = 0; frame < kReplayRepeats && result == ESP_OK;
         ++frame) {
      result = rmt_transmit(txChannel, txEncoder, txSymbols,
                            kCodeBits * sizeof(rmt_symbol_word_t),
                            &transmitConfig);
      if (result == ESP_OK) {
        result = rmt_tx_wait_all_done(txChannel, 1000);
      }
      if (frame + 1 < kReplayRepeats) {
        delayMicroseconds(kFrameGapUs);
      }
    }
    if (commandRepeat + 1 < commandRepeats) {
      delay(kHoldRepeatIntervalMs);
    }
  }

  xSemaphoreGive(txMutex);

  lastTransmitSucceeded.store(result == ESP_OK);
  statusVisible.store(true);
  statusShownAtMs.store(millis());
  screenDirty.store(true);
  Serial.printf(
      "RF_REPLAY action=%s code=0x%06lX frames=%u commands=%u source=%s "
      "result=%s\n",
      command.id, static_cast<unsigned long>(command.code), kReplayRepeats,
      commandRepeats, source,
      lastTransmitSucceeded.load() ? "sent" : "tx_failed");
  return lastTransmitSucceeded.load();
}

bool transmitCurtain(uint8_t curtain, uint8_t action, const char* source) {
  const curtain_rf_data::Waveform* waveform =
      findCurtainWaveform(curtain, action);
  if (!transmitterReady || waveform == nullptr ||
      waveform->edge_count % 2 != 0 ||
      waveform->edge_count / 2 > kMaxTxSymbols) {
    Serial.printf(
        "CURTAIN_RF_REPLAY curtain=%u action=%s source=%s result=invalid\n",
        curtain, curtainActionLabel(action), source);
    return false;
  }
  if (xSemaphoreTake(txMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
    Serial.printf(
        "CURTAIN_RF_REPLAY curtain=%u action=%s source=%s result=busy\n",
        curtain, curtainActionLabel(action), source);
    return false;
  }

  const size_t symbolCount = waveform->edge_count / 2;
  for (size_t symbol = 0; symbol < symbolCount; ++symbol) {
    txSymbols[symbol] = {
        .duration0 = waveform->durations[symbol * 2],
        .level0 = 1,
        .duration1 = waveform->durations[symbol * 2 + 1],
        .level1 = 0,
    };
  }
  rmt_transmit_config_t transmitConfig = {
      .loop_count = 0,
      .flags = {.eot_level = 0, .queue_nonblocking = 0},
  };
  esp_err_t result = rmt_transmit(
      txChannel, txEncoder, txSymbols,
      symbolCount * sizeof(rmt_symbol_word_t), &transmitConfig);
  if (result == ESP_OK) {
    result = rmt_tx_wait_all_done(txChannel, 5000);
  }
  xSemaphoreGive(txMutex);

  lastTransmitSucceeded.store(result == ESP_OK);
  statusVisible.store(true);
  statusShownAtMs.store(millis());
  screenDirty.store(true);
  Serial.printf(
      "CURTAIN_RF_REPLAY curtain=%u action=%s frames=%u symbols=%u source=%s "
      "result=%s\n",
      curtain, curtainActionLabel(action), waveform->frame_count,
      static_cast<unsigned>(symbolCount), source,
      result == ESP_OK ? "sent" : "tx_failed");
  return result == ESP_OK;
}

void publishEvent(const Command& command, const char* source, const char* requestId,
                  uint8_t repeats, bool sent) {
  if (!zenohConnected.load() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  JsonDocument event;
  event["protocol"] = kProtocolVersion;
  event["device_id"] = CX9_DEVICE_ID;
  event["request_id"] = requestId;
  event["command"] = command.id;
  event["code"] = command.code;
  event["source"] = source;
  event["repeat"] = repeats;
  event["status"] = sent ? "sent" : "tx_failed";
  event["hardware_confirmed"] = false;
  event["uptime_ms"] = millis();

  char payloadBuffer[384];
  const size_t payloadLength =
      serializeJson(event, payloadBuffer, sizeof(payloadBuffer));
  z_owned_bytes_t payload;
  z_bytes_copy_from_buf(&payload,
                        reinterpret_cast<const uint8_t*>(payloadBuffer),
                        payloadLength);
  z_view_keyexpr_t key;
  z_view_keyexpr_from_str_unchecked(&key, eventKey.c_str());
  const z_result_t result = z_put(z_session_loan(&zenohSession),
                                  z_view_keyexpr_loan(&key),
                                  z_bytes_move(&payload), nullptr);
  Serial.printf("ZENOH_EVENT action=%s result=%s\n", command.id,
                result == Z_OK ? "published" : "failed");
}

void replyJson(z_loaned_query_t* query, const String& key, JsonDocument& body) {
  char payloadBuffer[512];
  const size_t payloadLength =
      serializeJson(body, payloadBuffer, sizeof(payloadBuffer));
  z_owned_bytes_t payload;
  z_bytes_copy_from_buf(&payload,
                        reinterpret_cast<const uint8_t*>(payloadBuffer),
                        payloadLength);
  z_view_keyexpr_t replyKey;
  z_view_keyexpr_from_str_unchecked(&replyKey, key.c_str());
  z_query_reply(query, z_view_keyexpr_loan(&replyKey), z_bytes_move(&payload),
                nullptr);
}

void commandQueryHandler(z_loaned_query_t* query, void*) {
  lastZenohInboundAtMs.store(millis());
  JsonDocument response;
  response["protocol"] = kProtocolVersion;
  response["device_id"] = CX9_DEVICE_ID;
  response["hardware_confirmed"] = false;

  z_owned_string_t payloadString;
  if (z_bytes_to_string(z_query_payload(query), &payloadString) != Z_OK) {
    response["status"] = "rejected";
    response["reason"] = "payload_not_string";
    replyJson(query, commandKey, response);
    return;
  }

  JsonDocument request;
  const auto* data = z_string_data(z_string_loan(&payloadString));
  const size_t length = z_string_len(z_string_loan(&payloadString));
  const DeserializationError error = deserializeJson(request, data, length);
  const char* commandId = request["command"] | "";
  const char* requestId = request["request_id"] | "";
  uint8_t repeats = request["repeat"] | 1;
  const Command* command = findCommand(commandId);

  if (error || command == nullptr || repeats == 0 ||
      repeats > kMaxRemoteRepeat || (repeats > 1 && !command->repeatable)) {
    response["status"] = "rejected";
    response["reason"] = error ? "invalid_json" : "invalid_command_or_repeat";
    response["request_id"] = requestId;
    z_string_drop(z_string_move(&payloadString));
    replyJson(query, commandKey, response);
    return;
  }

  const bool sent = transmit(*command, "zenoh", repeats);
  response["request_id"] = requestId;
  response["command"] = command->id;
  response["repeat"] = repeats;
  response["status"] = sent ? "sent" : "tx_failed";
  publishEvent(*command, "zenoh", requestId, repeats, sent);
  z_string_drop(z_string_move(&payloadString));
  replyJson(query, commandKey, response);
}

void infoQueryHandler(z_loaned_query_t* query, void*) {
  lastZenohInboundAtMs.store(millis());
  JsonDocument response;
  response["protocol"] = kProtocolVersion;
  response["device_id"] = CX9_DEVICE_ID;
  response["model"] = "Humanex CX9-B-A RF433 bridge";
  response["firmware"] = kFirmwareVersion;
  response["transport"] = "zenoh-pico";
  response["rf_tx_gpio"] = static_cast<int>(kRfTxPin);
  response["rf_repeats"] = kReplayRepeats;
  response["hardware_feedback"] = false;
  JsonArray commands = response["commands"].to<JsonArray>();
  for (const auto& command : kCommands) {
    commands.add(command.id);
  }
  replyJson(query, infoKey, response);
}

void sendHttpJson(int statusCode, JsonDocument& body) {
  String payload;
  serializeJson(body, payload);
  curtainServer.send(statusCode, "application/json", payload);
}

void handleCurtainList() {
  JsonDocument response;
  response["device"] = "office-curtains";
  response["status"] = transmitterReady ? "ready" : "tx_not_ready";
  response["hardware_confirmed"] = false;
  response["catalog_sha256"] = curtain_rf_data::kSourceSha256;
  JsonArray curtains = response["curtains"].to<JsonArray>();
  for (uint8_t curtain = 1; curtain <= 5; ++curtain) {
    curtains.add(curtain);
  }
  sendHttpJson(200, response);
}

void handleHealth() {
  const uint32_t now = millis();
  const uint32_t lastInbound = lastZenohInboundAtMs.load();
  JsonDocument response;
  response["device"] = CX9_DEVICE_ID;
  response["firmware"] = kFirmwareVersion;
  response["uptime_ms"] = now;
  response["wifi_connected"] = WiFi.status() == WL_CONNECTED;
  response["ip_address"] = WiFi.localIP().toString();
  response["wifi_rssi"] = WiFi.RSSI();
  response["zenoh_connected"] = zenohConnected.load();
  response["zenoh_session_open_count"] = zenohSessionOpenCount.load();
  response["zenoh_inbound_timeout_count"] = zenohInboundTimeoutCount.load();
  if (lastInbound != 0) {
    response["last_zenoh_inbound_age_ms"] = now - lastInbound;
  } else {
    response["last_zenoh_inbound_age_ms"] = nullptr;
  }
  sendHttpJson(200, response);
}

void handleCurtainCommand(uint8_t curtain) {
  JsonDocument response;
  response["curtain"] = curtain;
  response["hardware_confirmed"] = false;

  JsonDocument request;
  const DeserializationError error =
      deserializeJson(request, curtainServer.arg("plain"));
  const char* command = request["command"] | "";
  const char* requestId = request["request_id"] | "";
  response["request_id"] = requestId;
  response["command"] = command;

  uint8_t action = UINT8_MAX;
  if (strcmp(command, "up") == 0) {
    action = 0;
  } else if (strcmp(command, "stop") == 0) {
    action = 1;
  } else if (strcmp(command, "down") == 0) {
    action = 2;
  }
  if (error || action == UINT8_MAX) {
    response["status"] = "rejected";
    response["reason"] = error ? "invalid_json" : "invalid_command";
    sendHttpJson(400, response);
    return;
  }

  const CurtainRequest queuedRequest = {curtain, action};
  if (curtainQueue == nullptr ||
      xQueueSend(curtainQueue, &queuedRequest, 0) != pdTRUE) {
    response["status"] = "rejected";
    response["reason"] = "queue_full";
    sendHttpJson(503, response);
    return;
  }
  response["status"] = "queued";
  response["queue_depth"] = uxQueueMessagesWaiting(curtainQueue);
  sendHttpJson(202, response);
}

void restartCurtainMdns() {
  if (curtainMdnsStarted) {
    MDNS.end();
  }
  curtainMdnsStarted = MDNS.begin(kCurtainHostname);
  if (curtainMdnsStarted) {
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("CURTAIN_MDNS result=failed");
  }
}

void setupCurtainHttp() {
  curtainServer.on("/", HTTP_GET, []() {
    curtainServer.send_P(200, "text/html; charset=utf-8", kCurtainPage);
  });
  curtainServer.on("/api/v1/curtains", HTTP_GET, handleCurtainList);
  curtainServer.on("/api/v1/health", HTTP_GET, handleHealth);
  for (uint8_t curtain = 1; curtain <= 5; ++curtain) {
    const String path = String("/api/v1/curtains/") + curtain + "/commands";
    curtainServer.on(path, HTTP_POST,
                     [curtain]() { handleCurtainCommand(curtain); });
  }
  curtainServer.onNotFound([]() {
    JsonDocument response;
    response["status"] = "not_found";
    sendHttpJson(404, response);
  });
  curtainServer.begin();
  restartCurtainMdns();
  curtainHttpStarted.store(true);
  Serial.printf("CURTAIN_HTTP_READY url=http://%s.local/ ip=http://%s/ catalog=%s\n",
                kCurtainHostname,
                WiFi.localIP().toString().c_str(),
                curtain_rf_data::kSourceSha256);
}

void curtainHttpTask(void*) {
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  setupCurtainHttp();
  bool wasConnected = true;
  for (;;) {
    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !wasConnected) {
      restartCurtainMdns();
    }
    if (connected) {
      curtainServer.handleClient();
    }
    wasConnected = connected;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void curtainTransmitTask(void*) {
  CurtainRequest request = {};
  for (;;) {
    if (xQueueReceive(curtainQueue, &request, portMAX_DELAY) == pdTRUE) {
      transmitCurtain(request.curtain, request.action, "http_queue");
    }
  }
}

void closeZenoh() {
  zenohConnected.store(false);
  if (liveTokenDeclared) {
    z_liveliness_token_drop(z_liveliness_token_move(&liveToken));
    liveTokenDeclared = false;
  }
  if (infoQueryableDeclared) {
    z_queryable_drop(z_queryable_move(&infoQueryable));
    infoQueryableDeclared = false;
  }
  if (commandQueryableDeclared) {
    z_queryable_drop(z_queryable_move(&commandQueryable));
    commandQueryableDeclared = false;
  }
  if (z_internal_session_check(&zenohSession)) {
    z_session_drop(z_session_move(&zenohSession));
  }
  screenDirty.store(true);
}

bool openZenoh() {
  z_owned_config_t config;
  z_config_default(&config);
  zp_config_insert(z_config_loan_mut(&config), Z_CONFIG_MODE_KEY, "client");
  zp_config_insert(z_config_loan_mut(&config), Z_CONFIG_CONNECT_KEY,
                   CX9_ZENOH_CONNECT);
  if (z_open(&zenohSession, z_config_move(&config), nullptr) < 0) {
    Serial.println("ZENOH_CONNECT result=open_failed");
    return false;
  }

  z_owned_closure_query_t commandCallback;
  z_closure_query(&commandCallback, commandQueryHandler, nullptr, nullptr);
  z_view_keyexpr_t commandKeyExpr;
  z_view_keyexpr_from_str_unchecked(&commandKeyExpr, commandKey.c_str());
  if (z_declare_queryable(z_session_loan(&zenohSession), &commandQueryable,
                          z_view_keyexpr_loan(&commandKeyExpr),
                          z_closure_query_move(&commandCallback), nullptr) < 0) {
    Serial.println("ZENOH_CONNECT result=command_queryable_failed");
    closeZenoh();
    return false;
  }
  commandQueryableDeclared = true;

  z_owned_closure_query_t infoCallback;
  z_closure_query(&infoCallback, infoQueryHandler, nullptr, nullptr);
  z_view_keyexpr_t infoKeyExpr;
  z_view_keyexpr_from_str_unchecked(&infoKeyExpr, infoKey.c_str());
  if (z_declare_queryable(z_session_loan(&zenohSession), &infoQueryable,
                          z_view_keyexpr_loan(&infoKeyExpr),
                          z_closure_query_move(&infoCallback), nullptr) < 0) {
    Serial.println("ZENOH_CONNECT result=info_queryable_failed");
    closeZenoh();
    return false;
  }
  infoQueryableDeclared = true;

  z_view_keyexpr_t liveKeyExpr;
  z_view_keyexpr_from_str_unchecked(&liveKeyExpr, liveKey.c_str());
  if (z_liveliness_declare_token(z_session_loan(&zenohSession), &liveToken,
                                 z_view_keyexpr_loan(&liveKeyExpr), nullptr) < 0) {
    Serial.println("ZENOH_CONNECT result=liveliness_failed");
    closeZenoh();
    return false;
  }
  liveTokenDeclared = true;
  lastZenohInboundAtMs.store(millis());
  zenohSessionOpenCount.fetch_add(1);
  zenohConnected.store(true);
  screenDirty.store(true);
  Serial.printf("ZENOH_CONNECT result=ready endpoint=%s prefix=%s\n",
                CX9_ZENOH_CONNECT, keyPrefix.c_str());
  return true;
}

void serviceNetwork() {
  if (strlen(CX9_WIFI_SSID) == 0 || strlen(CX9_ZENOH_CONNECT) == 0) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastNetworkAttemptAtMs >= kNetworkRetryMs) {
      lastNetworkAttemptAtMs = millis();
      WiFi.disconnect();
      WiFi.begin(CX9_WIFI_SSID, CX9_WIFI_PASSWORD);
      Serial.println("WIFI_CONNECT result=attempt");
    }
    return;
  }

  if (zenohConnected.load() &&
      z_session_is_closed(z_session_loan(&zenohSession))) {
    Serial.println("ZENOH_CONNECT result=session_closed");
    closeZenoh();
  }
  if (zenohConnected.load() &&
      millis() - lastZenohInboundAtMs.load() >= kZenohInboundTimeoutMs) {
    Serial.println("ZENOH_CONNECT result=inbound_timeout action=reopen");
    zenohInboundTimeoutCount.fetch_add(1);
    closeZenoh();
    lastNetworkAttemptAtMs = millis() - kNetworkRetryMs;
    return;
  }
  if (zenohConnected.load()) {
    zp_spin_once(z_session_loan(&zenohSession));
  }
  if (!zenohConnected.load() &&
      millis() - lastNetworkAttemptAtMs >= kNetworkRetryMs) {
    lastNetworkAttemptAtMs = millis();
    openZenoh();
  }
}

void networkTask(void*) {
  for (;;) {
    serviceNetwork();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void selectNext() {
  selectedIndex = (selectedIndex + 1) % kCommandCount;
  statusVisible.store(false);
  Serial.printf("RF_SELECT direction=next action=%s\n", selectedCommand().id);
  screenDirty.store(true);
}

void handleSelectButton(bool pressed) {
  const uint32_t now = millis();
  if (pressed && !buttonA.wasPressed) {
    buttonA.pressedAtMs = now;
  }
  if (!pressed && buttonA.wasPressed) {
    const bool held = now - buttonA.pressedAtMs >= kSelectLongPressMs;
    statusVisible.store(false);
    if (uiScreen == UiScreen::Home) {
      uiScreen = UiScreen::MonitorLight;
    } else if (held) {
      uiScreen = uiScreen == UiScreen::CurtainAction
                     ? UiScreen::CurtainSelect
                     : UiScreen::Home;
    } else if (uiScreen == UiScreen::MonitorLight) {
      selectNext();
    } else if (uiScreen == UiScreen::CurtainSelect) {
      selectedCurtain = (selectedCurtain + 1) % 5;
    } else {
      selectedCurtainAction = (selectedCurtainAction + 1) % 3;
    }
    screenDirty.store(true);
  }
  buttonA.wasPressed = pressed;
}

void handleSendButton(bool pressed) {
  const uint32_t now = millis();
  if (pressed && !buttonB.wasPressed) {
    buttonB.pressedAtMs = now;
    buttonB.lastRepeatMs = now;
    buttonB.holdTriggered = false;
  }

  if (pressed && uiScreen == UiScreen::MonitorLight &&
      selectedCommand().repeatable &&
      now - buttonB.pressedAtMs >= kHoldStartMs &&
      (!buttonB.holdTriggered ||
       now - buttonB.lastRepeatMs >= kHoldRepeatIntervalMs)) {
    const bool sent = transmit(selectedCommand(), "button_hold");
    publishEvent(selectedCommand(), "button_hold", "", 1, sent);
    buttonB.holdTriggered = true;
    buttonB.lastRepeatMs = now;
  }

  if (!pressed && buttonB.wasPressed && !buttonB.holdTriggered) {
    if (uiScreen == UiScreen::Home) {
      uiScreen = UiScreen::CurtainSelect;
      statusVisible.store(false);
      screenDirty.store(true);
    } else if (uiScreen == UiScreen::MonitorLight) {
      const bool sent = transmit(selectedCommand(), "button_click");
      publishEvent(selectedCommand(), "button_click", "", 1, sent);
    } else if (uiScreen == UiScreen::CurtainSelect) {
      uiScreen = UiScreen::CurtainAction;
      statusVisible.store(false);
      screenDirty.store(true);
    } else {
      transmitCurtain(selectedCurtain + 1, selectedCurtainAction,
                      "button_click");
    }
  }
  buttonB.wasPressed = pressed;
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
  M5.Power.setExtOutput(true);
  M5.Display.setRotation(3);
  M5.Display.setTextWrap(false);

  txMutex = xSemaphoreCreateMutex();
  curtainQueue = xQueueCreate(kCurtainQueueDepth, sizeof(CurtainRequest));
  keyPrefix = String("humanex/cx9-ba/") + CX9_DEVICE_ID;
  commandKey = keyPrefix + "/command";
  infoKey = keyPrefix + "/info";
  eventKey = keyPrefix + "/event";
  liveKey = keyPrefix + "/live";
  z_internal_session_null(&zenohSession);
  z_internal_queryable_null(&commandQueryable);
  z_internal_queryable_null(&infoQueryable);
  z_internal_liveliness_token_null(&liveToken);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(kCurtainHostname);
  WiFi.setSleep(false);
  lastNetworkAttemptAtMs = millis() - kNetworkRetryMs;

  transmitterReady = setupTransmitter();
  xTaskCreatePinnedToCore(networkTask, "cx9-network", 8192, nullptr, 1, nullptr,
                          0);
  xTaskCreatePinnedToCore(curtainHttpTask, "curtain-http", 8192, nullptr, 1,
                          nullptr, 0);
  xTaskCreatePinnedToCore(curtainTransmitTask, "curtain-tx", 4096, nullptr, 1,
                          nullptr, 1);
  renderScreen();
  Serial.printf(
      "STICKS3_RF_READY firmware=%s tx_gpio=9 cx9_commands=%u "
      "curtain_commands=%u tx=%s catalog=%s\n",
      kFirmwareVersion, static_cast<unsigned>(kCommandCount),
      static_cast<unsigned>(curtain_rf_data::kWaveformCount),
      transmitterReady ? "ready" : "failed",
      curtain_rf_data::kSourceSha256);
}

void loop() {
  M5.update();
  handleSelectButton(M5.BtnA.isPressed());
  handleSendButton(M5.BtnB.isPressed());
  if (statusVisible.load() &&
      millis() - statusShownAtMs.load() >= kStatusDisplayMs &&
      !M5.BtnB.isPressed()) {
    statusVisible.store(false);
    screenDirty.store(true);
  }
  if (screenDirty.load()) {
    renderScreen();
  }
  delay(5);
}
