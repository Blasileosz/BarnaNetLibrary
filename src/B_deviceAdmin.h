#pragma once

// Miscellaneous System APIs: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/misc_system_api.html

#define B_TASKID_DEVICEADMIN 4

#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_system.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_app_desc.h>

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>

#include <sys/time.h>

#include "B_BarnaNetCommand.h"
#include "B_time.h"

#define B_DEVICEADMIN_NVS_NAMESPACE "B_DEVADMIN"
#define B_DEVICEADMIN_NVS_BUFFER "B_DEVADMIN_STR" // Key must be shorter than 15 characters so that NVS works properly

#define B_DEVICEADMIN_NAME_MAX_LENGTH 16 // Including null terminator

enum B_DEVICEADMIN_COMMAND_IDS {
	B_DEVICEADMIN_COMMAND_DEVICEINFO,
	B_DEVICEADMIN_COMMAND_PROJECTINFO,
	B_DEVICEADMIN_COMMAND_SETNAME
};

struct B_DeviceAdminTaskParameter {
	B_addressMap_t* addressMap;
};

// static void LoadDeviceNameFromNVS()
// - Private function

// static void SaveDeviceNameToNVS()
// - Private function

// static void SetDeviceName(const char* const name)
// - Private function

void B_DeviceAdminTask(void* pvParameters);
