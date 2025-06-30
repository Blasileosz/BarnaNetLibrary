#include "B_wifi.h"

static const char* wifiTag = "BarnaNet - WIFI";

static esp_netif_t* netifHandle = NULL;
static esp_event_handler_instance_t wifiEventHandlerInstance;
static esp_event_handler_instance_t ipEventHandlerInstance;
static EventGroupHandle_t wifiEventGroup = 0;
static unsigned int connectionRetryCount = 0;

static void B_WifiEventHandler(void* args, esp_event_base_t eventBase, int32_t eventId, void* eventData)
{
	switch (eventId)
	{
	case WIFI_EVENT_WIFI_READY:
		ESP_LOGI(wifiTag, "Wi-Fi ready");
		break;
	case WIFI_EVENT_SCAN_DONE:
		ESP_LOGI(wifiTag, "Wi-Fi scan done");
		break;
	case WIFI_EVENT_STA_START:
		ESP_LOGI(wifiTag, "Connecting...");
		esp_wifi_connect();
		break;
	case WIFI_EVENT_STA_STOP:
		ESP_LOGI(wifiTag, "Stopped");
		break;
	case WIFI_EVENT_STA_CONNECTED:
		ESP_LOGI(wifiTag, "Connected");
		break;
	case WIFI_EVENT_STA_DISCONNECTED:
		ESP_LOGI(wifiTag, "Disconnected");
		if (connectionRetryCount++ < B_WIFI_MAX_RETRY) {
			ESP_LOGI(wifiTag, "Reconnecting... (%u/%u)", connectionRetryCount, B_WIFI_MAX_RETRY);
			esp_wifi_connect();
		} else {
			xEventGroupSetBits(wifiEventGroup, B_WIFI_FAIL);
		}
		break;
	case WIFI_EVENT_STA_AUTHMODE_CHANGE:
		ESP_LOGI(wifiTag, "Authmode changed");
		break;
	default:
		ESP_LOGW(wifiTag, "Unknown event");
		break;
	}
}

static void B_IpEventHandler(void* args, esp_event_base_t eventBase, int32_t eventId, void* eventData)
{
	switch (eventId)
	{
	case IP_EVENT_STA_GOT_IP:
		ip_event_got_ip_t* event = (ip_event_got_ip_t*)eventData;
		ESP_LOGI(wifiTag, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
		connectionRetryCount = 0;
		xEventGroupSetBits(wifiEventGroup, B_WIFI_OK);
		break;
	case IP_EVENT_STA_LOST_IP:
		ESP_LOGI(wifiTag, "Lost IP");
		break;
	case (IP_EVENT_GOT_IP6):
		ip_event_got_ip6_t *eventV6 = (ip_event_got_ip6_t*)eventData;
		ESP_LOGI(wifiTag, "Got IPv6: " IPV6STR, IPV62STR(eventV6->ip6_info.ip));
		connectionRetryCount = 0;
		xEventGroupSetBits(wifiEventGroup, B_WIFI_OK);
		break;
	default:
		ESP_LOGW(wifiTag, "Unknown IP event");
		break;
	}
}

esp_err_t B_WifiInit()
{
	// Initialize network interface
	ESP_ERROR_CHECK(esp_netif_init());

	// Start default event loop
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	// Set station interface wifi event handlers
	ESP_ERROR_CHECK(esp_wifi_set_default_wifi_sta_handlers());

	// Create default station mode wifi
	netifHandle = esp_netif_create_default_wifi_sta();
	if (netifHandle == NULL){
		ESP_LOGE(wifiTag, "Failed to create default WiFi STA interface");
		return ESP_FAIL;
	}

	// Fill wifi config data with default values
	wifi_init_config_t wifiConfigDefault = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifiConfigDefault));

	// Create event group for wifi handles
	wifiEventGroup = xEventGroupCreate();

	// Register handle to all wifi events
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &B_WifiEventHandler, NULL, &wifiEventHandlerInstance));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &B_IpEventHandler, NULL, &ipEventHandlerInstance));

	return ESP_OK;
}

esp_err_t B_WifiConnect()
{
	ESP_ERROR_CHECK(B_WifiInit());

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

	// Return the result
	if (handlerReturnBits & B_WIFI_OK)
		return B_WIFI_OK;

	return B_WIFI_FAIL;
}

void B_WifiDisconnect()
{
	ESP_ERROR_CHECK(esp_wifi_disconnect());
}

void B_WifiCleanup()
{
	if (wifiEventGroup)
		vEventGroupDelete(wifiEventGroup);

	ESP_ERROR_CHECK(esp_wifi_stop());
	ESP_ERROR_CHECK(esp_wifi_deinit());
	ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(netifHandle));
	esp_netif_destroy(netifHandle);

	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiEventHandlerInstance));
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ipEventHandlerInstance));
}
