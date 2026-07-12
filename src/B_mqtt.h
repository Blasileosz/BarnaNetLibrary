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

#define B_MQTT_TWIN_QUEUE_LENGTH 10
#define B_MQTT_TWIN_TASK_DEFAULT_STACK_SIZE 1024 * 3

// Time in milliseconds to wait for a task to reply
#define B_MQTT_REPLY_TIMEOUT 1000

#define B_MQTT_IOT_HUB_NAME	"BarnaNet-IoTHubwork"

#define B_MQTT_HOST		B_MQTT_IOT_HUB_NAME ".azure-devices.net"
#define B_MQTT_PORT		(8883)
#define B_MQTT_USERNAME	(B_MQTT_HOST "/" CONFIG_B_MQTT_DEVICE_ID "/?api-version=2020-09-30")
#define B_MQTT_B_URL	"mqtts://" B_MQTT_HOST ":" B_MQTT_PORT

#define B_MQTT_TWIN_TOPIC				"$iothub/twin/res/#"
#define B_MQTT_TWIN_RETRIEVE_TOPIC			"$iothub/twin/GET/?$rid=%d" // Start the synchronization (sprintf format!)
#define B_MQTT_TWIN_PATCH_TOPIC			"$iothub/twin/PATCH/#"
#define B_MQTT_TWIN_PATCH_REPORTED_TOPIC	"$iothub/twin/PATCH/properties/reported/?$rid=%d" // Send a report of the current state (sprintf format!)
#define B_MQTT_TWIN_RECEIVE_DESIRED_TOPIC	"$iothub/twin/PATCH/properties/desired/" // Receive a desired property update from Azure

#define B_MQTT_C2D_TOPIC			"devices/" CONFIG_B_MQTT_DEVICE_ID "/messages/devicebound/#"
#define B_MQTT_DIRECT_METHOD_TOPIC	"$iothub/methods/POST/#"

enum B_MQTT_COMMAND_IDS {
	B_MQTT_COMMAND_SEND_REPORTED, // Only set
	B_MQTT_COMMAND_COMPILE_AND_SEND_REPORTED, // Only set
	B_MQTT_COMMAND_SET_REPORT_INTERVAL // Only set
};

typedef enum {
	B_TWIN_EVENT_SYNCHRONIZATION,	// Message from Azure (Initial synchronization or resynchronization after a connection drop)
	B_TWIN_EVENT_PROCESS_DESIRED,	// Message from Azure (Process desired properties)
	B_TWIN_EVENT_SEND_REPORTED	// Command from other tasks (Send reported properties)
} B_twinEventType_t;

typedef struct {
	B_twinEventType_t type;
	char* payload; // Heap allocated buffer containing the JSON string, owned by the event receiver
	size_t payloadLen;
} B_twinEvent_t;

struct B_MQTTTaskParameter {
	B_addressMap_t* addressMap;

	const char* verificationCertificate; // PEM format
	const char* authenticationCertificate; // PEM format
	const char* authenticationKey; // PEM format

	size_t twinTaskStackSize;
	void (*SynchronizationCallback)(const char* json, size_t len);
	void (*DesiredStateCallback)(const char* json, size_t len);
	B_twinEvent_t (*ReportedStateCallback)();
};

struct B_MQTTTwinTaskParameter {
	B_addressMap_t* addressMap;

	void (*SynchronizationCallback)(const char* json, size_t len);
	void (*DesiredStateCallback)(const char* json, size_t len);
	B_twinEvent_t (*ReportedStateCallback)();
};

// The MQTT task function
// Expects the B_MQTTTaskParameter struct
// Primary purpose is to offload the twin data processing and periodic reporting from the main MQTT event loop, to avoid blocking it
// Listens for twin events (B_twinEvent_t) from other tasks via a queue, and calls the appropriate callback in response to each event
// It owns all the payload buffers sent via queue or returned by the ReportedStateCallback, and is responsible for freeing them after processing
// - Private function
// static void B_MQTTTwinTask(void* pvParameters)

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
