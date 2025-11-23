#pragma once

#define B_TASKID_MQTT 3

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_system.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>

#include <mqtt_client.h>
#include <esp_http_server.h>

#include <cJSON.h>

#include "B_BarnaNetCommand.h"

// Time in milliseconds to wait for a task to reply
#define B_MQTT_REPLY_TIMEOUT 1000

#define B_IOT_HUB_NAME	"BarnaNet-IoTHubwork"
#define B_DEVICE_ID		"BB0"

#define B_MQTT_HOST		B_IOT_HUB_NAME ".azure-devices.net"
#define B_MQTT_PORT		(8883)
#define B_MQTT_USERNAME	(B_MQTT_HOST "/" B_DEVICE_ID "/?api-version=2020-09-30")
#define B_MQTT_B_URL	"mqtts://" B_MQTT_HOST ":" B_MQTT_PORT

#define B_C2D_TOPIC			"devices/" B_DEVICE_ID "/messages/devicebound/#"
#define B_DIRECT_METHOD_TOPIC	"$iothub/methods/POST/#"

struct B_MQTTTaskParameter {
	B_addressMap_t* addressMap;
	uint8_t* verificationCertificate; // PEM format
	uint8_t* authenticationCertificate; // PEM format
	uint8_t* authenticationKey; // PEM format
};

// Intermediate function to relay a command received from MQTT to the appropriate task
// Sanitizes the command
// - Private function
// static void B_MQTTRelayCommand(B_command_t* const command, uint8_t transmissionID)

// Handles incoming C2D messages
// Called by B_RouteMQTTMessage
// The data is in raw bytes as we need it
// - Private function
// static void B_HandleC2DMessage(esp_mqtt_client_handle_t client, int topicLen, char* topic, int dataLen, char* data)

// Handles incoming Direct Method invocations
// Called by B_RouteMQTTMessage
// The data is a JSON array of bytes representing the command struct
// - Private function
// static void B_HandleDirectMethod(esp_mqtt_client_handle_t client, int topicLen, char* topic, int dataLen, char* data)

// Serializes the command into a JSON array and sends it as a Direct Method response
// The responseCommand's transmissionID field is used as the $rid in the topic
// - Private function
// static void B_SendDirectMethodResponse(B_command_t* const responseCommand)

// Routes the incoming MQTT message to the appropriate handler based on the topic
// Called by B_MQTTHandler
// - Private function
// static void B_RouteMQTTMessage(esp_mqtt_client_handle_t client, int topicLen, char* topic, int dataLen, char* data)

// Test function to send a binary message to the broker
// - Private function
// static void B_SendMQTTBinary(esp_mqtt_client_handle_t client)

// This function is called by the MQTT client event loop
// It handles incoming MQTT events and dispatches them to the appropriate handlers
// - handlerArgs user data registered to the event.
// - base Event base for the handler (always MQTT Base in this example).
// - eventID The id for the received event.
// - eventData The data for the event, esp_mqtt_event_handle_t.
// - Private function
// static void B_MQTTHandler(void* handlerArgs, esp_event_base_t base, int32_t eventID, void* eventData)

// Initialize the MQTT client and initiates connecting to the broker
// - Private function
// static void B_MQTTInit(B_addressMap_t* addressMap)

// The MQTT task function
// Expects the B_MQTTTaskParameter struct
void B_MQTTTask(void* pvParameters);

void B_MQTTCleanup();
