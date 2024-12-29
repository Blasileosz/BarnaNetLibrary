#include "B_wifi.h"

static const char* wifiTag = "BarnaNet - WIFI";

static EventGroupHandle_t wifiEventGroup = 0;
static unsigned int connectionRetryCount = 0;

static void B_WifiEventHandler(void* args, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
	if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_START) {
		ESP_LOGI(wifiTag, "Connecting");
		esp_wifi_connect();
	} else if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED) {
		if (connectionRetryCount++ < B_MAX_RETRY) {
			ESP_LOGI(wifiTag, "Reconnecting %u", connectionRetryCount);
			esp_wifi_connect();
		} else {
			xEventGroupSetBits(wifiEventGroup, B_WIFI_FAIL);
		}
	}
}

static void B_IpEventHandler(void* args, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
	if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) eventData;
		ESP_LOGI(wifiTag, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
		connectionRetryCount = 0;
		xEventGroupSetBits(wifiEventGroup, B_WIFI_OK);
	}
}

esp_err_t B_WifiConnect() {	
	// Initialize network interface
	ESP_ERROR_CHECK(esp_netif_init());

	// Start default event loop
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	// Create default station mode wifi
	esp_netif_create_default_wifi_sta();

	// Fill wifi config data with default values
	wifi_init_config_t wifiConfigDefault = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifiConfigDefault));

	// Create event group for wifi handles
	wifiEventGroup = xEventGroupCreate();

	// Register handle to all wifi events
	esp_event_handler_instance_t wifiEventHandlerInstance;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &B_WifiEventHandler, NULL, &wifiEventHandlerInstance));

	esp_event_handler_instance_t gotIpEventHandlerInstance;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &B_IpEventHandler, NULL, &gotIpEventHandlerInstance));

	wifi_config_t wifiConfig = {
		.sta = {
			.ssid = B_AP_SSID,
			.password = B_AP_PASS,
			.threshold.authmode = WIFI_AUTH_WPA2_PSK,
			.pmf_cfg = {
				.capable = true,
				.required = false
			}
		}
	};
	
	// Set controller to station mode
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

	// Apply config to controller
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifiConfig));

	// Start wifi driver
	ESP_ERROR_CHECK(esp_wifi_start());
	ESP_LOGI(wifiTag, "Station wifi driver started");

	// Wait for event handlers to return
	EventBits_t handlerReturnBits = xEventGroupWaitBits(wifiEventGroup, B_WIFI_OK | B_WIFI_FAIL, pdFALSE, pdFALSE, portMAX_DELAY);

	// Unregister handlers
	ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEventHandlerInstance));
	ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, gotIpEventHandlerInstance));
	vEventGroupDelete(wifiEventGroup);

	// Return the result
	if (handlerReturnBits & B_WIFI_OK)
		return B_WIFI_OK;

	return B_WIFI_FAIL;
}
