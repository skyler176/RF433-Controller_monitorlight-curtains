#pragma once

// Copy to config.local.h. The local file is ignored by git.
#define WIFI_SSID "your-2.4ghz-wifi"
#define WIFI_PASSWORD "your-wifi-password"

#define MQTT_BROKER_HOST "192.168.1.100"
#define MQTT_BROKER_PORT 1883
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""

#define DEVICE_ID "humanex-cx9-ba-sticks3"
#define MQTT_CLIENT_ID "humanex-cx9-ba-sticks3"

#define MQTT_COMMAND_TOPIC "humanex-monitorlight/ir/cx9-ba/command"
#define MQTT_STATE_TOPIC "humanex-monitorlight/ir/cx9-ba/state"
#define MQTT_EVENT_TOPIC "humanex-monitorlight/ir/cx9-ba/event"
#define MQTT_PRESENCE_TOPIC "humanex-monitorlight/ir/cx9-ba/presence"

