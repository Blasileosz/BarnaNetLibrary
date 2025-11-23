#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_system.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>

#define B_WIFI_MAX_RETRY 20
#define B_WIFI_OK 0b1
#define B_WIFI_FAIL 0b10

// Handles events regarding connecting and reconnecting to the network
// - Private function
// static void B_WifiEventHandler();

// Handles the IP receive event
// - Private function
// static void B_IpEventHandler();

esp_err_t B_WifiInit();

// Connect to the WIFI network using the credentials in B_SECRET.h
esp_err_t B_WifiConnect(const char* ssid, const char* password);

void B_WifiDisconnect();
void B_WifiCleanup();
