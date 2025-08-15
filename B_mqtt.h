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

#include "B_SECRET.h"

#include "B_BarnaNetCommand.h"

// Time in miliseconds to wait for a task to reply
#define B_MQTT_REPLY_TIMEOUT 1000

#define B_IOT_HUB_NAME	"BarnaNet-IoTHubwork"
#define B_DEVICE_ID		"BB0"

#define B_MQTT_HOST		B_IOT_HUB_NAME ".azure-devices.net"
#define B_MQTT_PORT		(8883)
#define B_MQTT_USERNAME	(B_MQTT_HOST "/" B_DEVICE_ID "/?api-version=2020-09-30")
#define B_MQTT_B_URL	"mqtts://" B_MQTT_HOST ":" B_MQTT_PORT


#if CONFIG_BROKER_CERTIFICATE_OVERRIDDEN == 1
static const uint8_t _binary_DigiCertGlobalRootG2_crt_pem_start[]  = "-----BEGIN CERTIFICATE-----\n" CONFIG_BROKER_CERTIFICATE_OVERRIDE "\n-----END CERTIFICATE-----";
#else
extern const uint8_t _binary_DigiCertGlobalRootG2_crt_pem_start[]	asm("_binary_DigiCertGlobalRootG2_crt_pem_start");
#endif
extern const uint8_t _binary_DigiCertGlobalRootG2_crt_pem_end[]	asm("_binary_DigiCertGlobalRootG2_crt_pem_end");

extern const uint8_t _binary_BarnaNet_CA_crt_start[]	asm("_binary_BarnaNet_CA_crt_start");
extern const uint8_t _binary_BarnaNet_CA_crt_end[]	asm("_binary_BarnaNet_CA_crt_end");

extern const uint8_t _binary_BB0_crt_start[]	asm("_binary_BB0_crt_start");
extern const uint8_t _binary_BB0_crt_end[]	asm("_binary_BB0_crt_end");

extern const uint8_t _binary_BB0_key_start[]	asm("_binary_BB0_key_start");
extern const uint8_t _binary_BB0_key_end[]	asm("_binary_BB0_key_end");

#define B_C2D_TOPIC			"devices/" B_DEVICE_ID "/messages/devicebound/#"
#define B_DIRECT_METHOD_TOPIC	"$iothub/methods/POST/#"

//static void B_MQTTHandler(void *handlerArgs, esp_event_base_t base, int32_t eventID, void *eventData)

struct B_MQTTTaskParameter {
	B_addressMap_t* addressMap;
};

// Not a task
void B_MQTTStart(struct B_MQTTTaskParameter* taskParameter);

// Task
// Function unused until transmition ID is implemented into the command struct
void B_MQTTTask(void* pvParameters);

void B_MQTTCleanup();
