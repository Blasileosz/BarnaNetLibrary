#include "B_deviceAdmin.h"

static const char* deviceAdminTag = "BarnaNet - DeviceAdmin";

static char deviceName[B_DEVICEADMIN_NAME_MAX_LENGTH] = CONFIG_B_MQTT_DEVICE_ID; // Default name is the MQTT device ID
static_assert(sizeof(CONFIG_B_MQTT_DEVICE_ID) < B_DEVICEADMIN_NAME_MAX_LENGTH, "MQTT device ID is too long to fit as default device name");

static void LoadDeviceNameFromNVS()
{
	nvs_handle_t nvsHandle;
	esp_err_t err = nvs_open(B_DEVICEADMIN_NVS_NAMESPACE, NVS_READONLY, &nvsHandle);
	
	// Namespace not yet initialized, it does by saving a value to it
	if (err == ESP_ERR_NVS_NOT_FOUND) {
		ESP_LOGI(deviceAdminTag, "NVS namespace not yet initialized");
		return;
	}
	else ESP_ERROR_CHECK(err);

	// Read the the device name buffer size
	size_t requiredSize = 0;
	err = nvs_get_str(nvsHandle, B_DEVICEADMIN_NVS_BUFFER, NULL, &requiredSize);
	if (requiredSize == 0 || err == ESP_ERR_NVS_NOT_FOUND) {
		ESP_LOGI(deviceAdminTag, "NVS buffer not yet initialized");
		nvs_close(nvsHandle);
		return;
	}
	else ESP_ERROR_CHECK(err);

	// Check if the stored size differs from the current max length
	if (requiredSize != B_DEVICEADMIN_NAME_MAX_LENGTH) {
		ESP_LOGW(deviceAdminTag, "Stored device name size differs current max length, erasing flash");
		nvs_flash_erase();
		nvs_close(nvsHandle);
		return;
	}

	err = nvs_get_str(nvsHandle, B_DEVICEADMIN_NVS_BUFFER, deviceName, &requiredSize);
	if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
		ESP_ERROR_CHECK(err);

	nvs_close(nvsHandle);

	ESP_LOGI(deviceAdminTag, "Loaded device name from NVS: %s", deviceName);
}

static void SaveDeviceNameToNVS()
{
	nvs_handle_t nvsHandle;
	ESP_ERROR_CHECK(nvs_open(B_DEVICEADMIN_NVS_NAMESPACE, NVS_READWRITE, &nvsHandle));

	ESP_ERROR_CHECK(nvs_set_str(nvsHandle, B_DEVICEADMIN_NVS_BUFFER, deviceName));
	ESP_ERROR_CHECK(nvs_commit(nvsHandle));
	nvs_close(nvsHandle);

	ESP_LOGI(deviceAdminTag, "Saved device name to NVS: %s", deviceName);
}

static void SetDeviceName(const char* const name)
{
	// Create a temporary copy of the name
	char newName[B_DEVICEADMIN_NAME_MAX_LENGTH] = { 0 };
	strncpy(newName, name, B_DEVICEADMIN_NAME_MAX_LENGTH - 1);

	// Sanitize the name, replace non-printable characters with space
	for (int i = 0; i < B_DEVICEADMIN_NAME_MAX_LENGTH - 1 && newName[i] != '\0'; i++) {
		if (newName[i] < 32 || newName[i] > 126) {
			newName[i] = ' ';
		}
	}

	// Update device name
	memcpy(deviceName, newName, B_DEVICEADMIN_NAME_MAX_LENGTH);
	ESP_LOGI(deviceAdminTag, "Set new device name: %s", deviceName);

	// Save to NVS
	SaveDeviceNameToNVS();
}

void B_DeviceAdminTask(void* pvParameters)
{
	const struct B_DeviceAdminTaskParameter* const taskParameter = (const struct B_DeviceAdminTaskParameter* const)pvParameters;
	if (taskParameter == NULL || taskParameter->addressMap == NULL) {
		ESP_LOGE(deviceAdminTag, "The device admin task parameter is invalid, aborting startup");
		vTaskDelete(NULL);
	}

	QueueHandle_t deviceAdminQueue = B_GetAddress(taskParameter->addressMap, B_TASKID_DEVICEADMIN);
	if (deviceAdminQueue == B_ADDRESS_MAP_INVALID_QUEUE) {
		ESP_LOGE(deviceAdminTag, "The device admin queue is invalid, aborting startup");
		vTaskDelete(NULL);
	}

	LoadDeviceNameFromNVS();

	while (true) {
		// Block until a command is received
		B_command_t command = { 0 };
		xQueueReceive(deviceAdminQueue, (void* const)&command, portMAX_DELAY);

		uint8_t commandOP = B_COMMAND_OP(command.header);
		uint8_t commandID = B_COMMAND_ID(command.header);

		// Device Info Command
		if (commandOP == B_COMMAND_OP_GET && commandID == B_DEVICEADMIN_COMMAND_DEVICEINFO) {
			B_command_t responseCommand = { 0 };

			time_t systemTime;
			struct tm timeinfo = { 0 };
			char timeString[32] = { 0 };
			time(&systemTime);
			localtime_r(&systemTime, &timeinfo);
			strftime(timeString, sizeof(timeString), "%Y. %m. %d. - %X", &timeinfo);
			
			uint32_t freeHeap = esp_get_free_heap_size();

			// Fill response body
			size_t offset = 0;

			B_FillCommandBodyString(&responseCommand, offset, deviceName, B_DEVICEADMIN_NAME_MAX_LENGTH);
			offset += B_DEVICEADMIN_NAME_MAX_LENGTH;

			B_FillCommandBodyString(&responseCommand, offset, timeString, sizeof(timeString));
			offset += sizeof(timeString);

			B_FillCommandBody_DWORD(&responseCommand, offset, freeHeap);
			offset += sizeof(uint32_t);

			// Send back response
			if (!B_SendReplyCommand(taskParameter->addressMap, &command, &responseCommand, B_TASKID_DEVICEADMIN)) {
				ESP_LOGE(deviceAdminTag, "Failed to send Device Info data back to sender");
			}
		}

		// Project Info Command
		else if (commandOP == B_COMMAND_OP_GET && commandID == B_DEVICEADMIN_COMMAND_PROJECTINFO) {
			B_command_t responseCommand = { 0 };

			const esp_app_desc_t* appDesc = esp_app_get_description();

			// Fill response body
			size_t offset = 0;

			B_FillCommandBodyString(&responseCommand, offset, appDesc->idf_ver, sizeof(appDesc->idf_ver));
			offset += sizeof(appDesc->idf_ver);

			B_FillCommandBodyString(&responseCommand, offset, appDesc->project_name, sizeof(appDesc->project_name));
			offset += sizeof(appDesc->project_name);

			B_FillCommandBodyString(&responseCommand, offset, appDesc->version, sizeof(appDesc->version));
			offset += sizeof(appDesc->version);

			// Send back response
			if (!B_SendReplyCommand(taskParameter->addressMap, &command, &responseCommand, B_TASKID_DEVICEADMIN)) {
				ESP_LOGE(deviceAdminTag, "Failed to send Device Info data back to sender");
			}
		}

		// Set Name Command
		else if (commandOP == B_COMMAND_OP_SET && commandID == B_DEVICEADMIN_COMMAND_SETNAME) {
			
			// Read name from command body
			SetDeviceName((const char*)&command.body[0]);

			// Send back OK response
			B_SendStatusReply(taskParameter->addressMap, &command, B_TASKID_DEVICEADMIN, B_COMMAND_OP_RES, "OK");
		}

		// Invalid command
		else {
			B_SendStatusReply(taskParameter->addressMap, &command, B_TASKID_DEVICEADMIN, B_COMMAND_OP_ERR, "Invalid command");
		}
	}

	// Task paniced, clean up and delete task
	vTaskDelete(NULL);
}
