#include "B_mqtt.h"

static const char* mqttTag = "BarnaNet - MQTT";
static B_addressMap_t* addressMapPointer = NULL; // If the MQTT handler was an ISR, this would not work (it would need to be passed into it)
static esp_mqtt_client_handle_t mqttClient;

static void B_MQTTRelayCommand(B_command_t* const command, B_command_t* const responseCommand)
{
	// Sanitize request type
	if (B_COMMAND_OP(command->header) == B_COMMAND_OP_RES || B_COMMAND_OP(command->header) == B_COMMAND_OP_ERR) {
		ESP_LOGW(mqttTag, "Invalid request type");
		B_FillCommandHeader(responseCommand, B_TASKID_MQTT, command->dest, B_COMMAND_OP_ERR, B_COMMAND_ID(command->header));
		B_FillCommandBodyString(responseCommand, "Invalid request type");
		return;
	}

	// Select task to relay command
	QueueHandle_t destAddress = B_GetAddress(addressMapPointer, command->dest);
	// TODO: Rename address map to queue map
	// TODO: Make a helper function in the address map file to validate the destination address
	if (destAddress == NULL || command->dest == 1 /* TCP */ || command->dest == B_TASKID_MQTT) {
		ESP_LOGW(mqttTag, "Invalid DEST");
		B_FillCommandHeader(responseCommand, B_TASKID_MQTT, command->dest, B_COMMAND_OP_ERR, B_COMMAND_ID(command->header));
		B_FillCommandBodyString(responseCommand, "Invalid DEST");
		return;
	}

	command->from = B_TASKID_MQTT;

	// Relay the command to the selected task
	if (xQueueSend(destAddress, (void* const)command, 0) != pdPASS) {
		ESP_LOGE(mqttTag, "Command unable to be inserted into the queue");
		B_FillCommandHeader(responseCommand, B_TASKID_MQTT, command->dest, B_COMMAND_OP_ERR, B_COMMAND_ID(command->header));
		B_FillCommandBodyString(responseCommand, "INTERNAL: Relay error");
		return;
	}

	// Wait for reply
	QueueHandle_t mqttQueue = B_GetAddress(addressMapPointer, B_TASKID_MQTT);
	if (xQueueReceive(mqttQueue, (void* const)responseCommand, pdMS_TO_TICKS(B_MQTT_REPLY_TIMEOUT)) != pdPASS) {
		ESP_LOGE(mqttTag, "Recipient did not reply to the command");
		B_FillCommandHeader(responseCommand, B_TASKID_MQTT, command->dest, B_COMMAND_OP_ERR, B_COMMAND_ID(command->header));
		B_FillCommandBodyString(responseCommand, "Recipient did not reply to the command");
	}
}

static void B_HandleC2DMessage(esp_mqtt_client_handle_t client, int topicLen, char* topic, int dataLen, char* data)
{
	ESP_LOGI(mqttTag, "Received C2D message");

	if (dataLen <= 0 || dataLen >= B_COMMAND_STRUCT_SIZE) {
		ESP_LOGE(mqttTag, "Invalid data received");
		return;
	}

	// The data should be in raw bytes as we need it
	B_command_t command = { 0 };
	memcpy((void*)&command, data, sizeof(B_command_t));
	ESP_LOGI(mqttTag, "Command from: %u, dest: %u, header: %u", command.from, command.dest, command.header);

	B_command_t replyCommand = { 0 }; // C2D messages don't take replies, but cannot be NULL as the helper functions don't expect that
	B_MQTTRelayCommand(&command, &replyCommand);
	// Don't send reply
}

static void B_HandleDirectMethod(esp_mqtt_client_handle_t client, int topicLen, char* topic, int dataLen, char* data)
{
	ESP_LOGI(mqttTag, "Handling Direct Method");

	// // Construct a httpd_req_t struct from the topic URI
	// if (topicLen > 512) {
	// 	ESP_LOGE(mqttTag, "MQTT topic URI longer than should be");
	// 	return;
	// }

	// httpd_req_t topicStruct = { 0 };
	// ESP_LOGI(mqttTag, "Topic buffer: %s", topicStruct.uri);
	// strcpy(topicStruct.uri, topic);

	// Find the query start in the URI
	char* queryStart = strstr(topic, "?");
	if (!queryStart) {
		ESP_LOGE(mqttTag, "Query part not found");
		return;
	}
	queryStart += 1; // Exclude the questionmark
	int offset = queryStart - topic;
	
	char queryBuffer[512] = { 0 };
	memcpy(queryBuffer, queryStart, topicLen - offset);

	// Parse the topic URI using the http server library

	// If topic was shorter than 512, this must also be but just in case
	// if (httpd_req_get_url_query_len(&topicStruct) > sizeof(queryBuffer) / sizeof(char)) {
	// 	ESP_LOGE(mqttTag, "MQTT topic query string longer than should be");
	// 	return;
	// }

	// ESP_ERROR_CHECK(httpd_req_get_url_query_str(&topicStruct, queryBuffer, sizeof(queryBuffer) / sizeof(char)));

	// TODO: string length checks

	ESP_LOGI(mqttTag, "Query buffer: %s", queryBuffer);

	char valueBuffer[32] = { 0 };
	ESP_ERROR_CHECK(httpd_query_key_value(queryBuffer, "$rid", valueBuffer, sizeof(valueBuffer) / sizeof(char)));

	int responseID = atoi(valueBuffer);

	ESP_LOGI(mqttTag, "$rid=%s (%i)", valueBuffer, responseID);

	const char* responseData = "\"hello\"";
	char replyTopic[64] = { 0 };

	if ((strlen("$iothub/methods/res/200/?$rid=") + strlen(valueBuffer) + 1) > sizeof(replyTopic)) {
		ESP_LOGE(mqttTag, "MQTT reply topic URI longer than expected");
		return;
	}
	
	strcpy(replyTopic, "$iothub/methods/res/200/?$rid=");
	strcat(replyTopic, valueBuffer);

	

	// Use CJSON to deserialize the data, the JSON root should be an array
	cJSON* root = cJSON_Parse(data);
	if (!root) {
		ESP_LOGE(mqttTag, "Failed to parse JSON");
		return;
	}

	if (!cJSON_IsArray(root)) {
		ESP_LOGE(mqttTag, "Expected array");
		cJSON_Delete(root);
		return;
	}

	B_command_t command = { 0 };

	// Process the array elements
	cJSON* element = NULL;
	int elementIndex = 0;
	cJSON_ArrayForEach(element, root) {
		elementIndex++;

		if (elementIndex >= B_COMMAND_STRUCT_SIZE) {
			ESP_LOGE(mqttTag, "Array element index %d exceeds command body size", elementIndex);
			break;
		}

		if (!cJSON_IsNumber(element)) {
			ESP_LOGE(mqttTag, "Expected number");
			continue;
		}

		if (element->valueint < 0 || element->valueint > 255) {
			ESP_LOGE(mqttTag, "Array element value %d out of range", element->valueint);
			continue;
		}

		((uint8_t*)&command)[elementIndex - 1] = (uint8_t)element->valueint;
		ESP_LOGI(mqttTag, "Array element: %d", element->valueint);
	}

	ESP_LOGI(mqttTag, "Command from: %u, dest: %u, header: %u", command.from, command.dest, command.header);

	cJSON_Delete(root);

	// Relay the command
	B_command_t replyCommand = { 0 };
	B_MQTTRelayCommand(&command, &replyCommand);

	// Serialize replyCommand
	cJSON *json_array = cJSON_CreateArray();
	if (!json_array)
		return;

	for (size_t i = 0; i < sizeof(B_command_t); ++i) {
		cJSON_AddItemToArray(json_array, cJSON_CreateNumber(((uint8_t*)&replyCommand)[i]));
	}

	char *json_str = cJSON_PrintUnformatted(json_array); // You may use cJSON_Print for pretty print
	cJSON_Delete(json_array);

	int msg_id = esp_mqtt_client_publish(client, replyTopic, json_str, strlen(json_str), 0, 0);
	ESP_LOGI(mqttTag, "MQTT reply sent with msg_id=%d", msg_id);

	free(json_str);
}

static void B_RouteMQTTMessage(esp_mqtt_client_handle_t client, int topicLen, char* topic, int dataLen, char* data)
{
	// Route the topic to the appropriate handler (remove the # and the null terminator from the end)
	if (strncmp(topic, B_C2D_TOPIC, sizeof(B_C2D_TOPIC) - 2) == 0) {
		B_HandleC2DMessage(client, topicLen, topic, dataLen, data);

	} else if (strncmp(topic, B_DIRECT_METHOD_TOPIC, sizeof(B_DIRECT_METHOD_TOPIC) - 2) == 0) {
		B_HandleDirectMethod(client, topicLen, topic, dataLen, data);

	} else {
		ESP_LOGW(mqttTag, "Unhandled topic: %.*s", topicLen, topic);
	}
}

static void B_SendMQTTBinary(esp_mqtt_client_handle_t client)
{
	const char* response = "Hello back";
	int msg_id = esp_mqtt_client_publish(client, "/topic/binary", response, 11, 0, 0);
	ESP_LOGI(mqttTag, "binary sent with msg_id=%d", msg_id);
}

// This function is called by the MQTT client event loop.
// - handlerArgs user data registered to the event.
// - base Event base for the handler (always MQTT Base in this example).
// - eventID The id for the received event.
// - eventData The data for the event, esp_mqtt_event_handle_t.
static void B_MQTTHandler(void* handlerArgs, esp_event_base_t base, int32_t eventID, void* eventData)
{
	ESP_LOGD(mqttTag, "Event dispatched from event loop base=%s, eventID=%" PRIi32, base, eventID);
	esp_mqtt_event_handle_t event = eventData;
	esp_mqtt_client_handle_t client = event->client;
	int messageID = 0;

	switch ((esp_mqtt_event_id_t)eventID) {
		case MQTT_EVENT_CONNECTED:
			ESP_LOGI(mqttTag, "MQTT_EVENT_CONNECTED");

			// Subscribe to topics
			messageID = esp_mqtt_client_subscribe(client, B_C2D_TOPIC, 0);
			ESP_LOGI(mqttTag, "Sent subscribe request to %s, messageID=%d", B_C2D_TOPIC, messageID);

			messageID = esp_mqtt_client_subscribe(client, B_DIRECT_METHOD_TOPIC, 0);
			ESP_LOGI(mqttTag, "Sent subscribe request to %s, messageID=%d", B_DIRECT_METHOD_TOPIC, messageID);
			break;

		case MQTT_EVENT_DISCONNECTED:
			ESP_LOGI(mqttTag, "MQTT_EVENT_DISCONNECTED");
			break;

		case MQTT_EVENT_SUBSCRIBED:
			ESP_LOGI(mqttTag, "MQTT_EVENT_SUBSCRIBED, messageID=%d", event->msg_id);
			break;

		case MQTT_EVENT_UNSUBSCRIBED:
			ESP_LOGI(mqttTag, "MQTT_EVENT_UNSUBSCRIBED, messageID=%d", event->msg_id);
			break;

		case MQTT_EVENT_PUBLISHED:
			ESP_LOGI(mqttTag, "MQTT_EVENT_PUBLISHED, messageID=%d", event->msg_id);
			break;

		case MQTT_EVENT_DATA:
			ESP_LOGI(mqttTag, "MQTT_EVENT_DATA");
			printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
			printf("DATA=%.*s\r\n", event->data_len, event->data);

			B_RouteMQTTMessage(client, event->topic_len, event->topic, event->data_len, event->data);
			break;

		case MQTT_EVENT_ERROR:
			ESP_LOGI(mqttTag, "MQTT_EVENT_ERROR");
			if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
				ESP_LOGI(mqttTag, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
				ESP_LOGI(mqttTag, "Last tls stack error responseID: 0x%x", event->error_handle->esp_tls_stack_err);
				ESP_LOGI(mqttTag, "Last captured errno : %d (%s)", event->error_handle->esp_transport_sock_errno,
					strerror(event->error_handle->esp_transport_sock_errno));
			}
			else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
				ESP_LOGI(mqttTag, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
			}
			else {
				ESP_LOGW(mqttTag, "Unknown error type: 0x%x", event->error_handle->error_type);
			}
			break;

		default:
			ESP_LOGI(mqttTag, "Other event id:%d", event->event_id);
			break;
	}
}

static void B_MQTTInit()
{
	// ESP_LOGI(mqttTag, "CA cert: %s", _binary_BarnaNet_CA_crt_start);
	// ESP_LOGI(mqttTag, "Client cert: %s", _binary_BB0_crt_start);
	// ESP_LOGI(mqttTag, "Client key: %s", _binary_BB0_key_start);
	const esp_mqtt_client_config_t mqttConfig = {
		.broker = {
			//.address.uri = B_MQTT_B_URL, // This line could replace the hostname, port and transport
			.address.hostname = B_MQTT_HOST,
			.address.port = B_MQTT_PORT,
			.address.transport = MQTT_TRANSPORT_OVER_SSL,
			.verification.certificate = (const char*)_binary_DigiCertGlobalRootG2_crt_pem_start,
			//.verification.certificate = (const char*)_binary_BarnaNet_CA_crt_start,
			//.verification.use_global_ca_store = false,
			.verification.skip_cert_common_name_check = false // Do not verify the server sertificate chain
		},
		.credentials = {
			.username = B_MQTT_USERNAME,
			//.authentication.password = B_AZURE_SAS,
			.client_id = B_DEVICE_ID,
			.authentication.certificate = (const char*)_binary_BB0_crt_start,
			.authentication.key = (const char*)_binary_BB0_key_start
		}
	};
	mqttClient = esp_mqtt_client_init(&mqttConfig);

	// The last argument may be used to pass data to the event handler
	ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqttClient, ESP_EVENT_ANY_ID, B_MQTTHandler, NULL));
	ESP_ERROR_CHECK(esp_mqtt_client_start(mqttClient));

	ESP_LOGI(mqttTag, "Initialized MQTT");
}

void B_MQTTStart(struct B_MQTTTaskParameter* taskParameter)
{
	if (taskParameter == NULL || taskParameter->addressMap == NULL) {
		ESP_LOGE(mqttTag, "The MQTT task parameter is invalid, aborting startup");
		return;
	}

	addressMapPointer = taskParameter->addressMap;

	B_MQTTInit();
}

void B_MQTTTask(void* pvParameters)
{
	const struct B_MQTTTaskParameter* const taskParameter = (const struct B_MQTTTaskParameter* const)pvParameters;
	if (taskParameter == NULL || taskParameter->addressMap == NULL) {
		ESP_LOGE(mqttTag, "The MQTT task parameter is invalid, aborting startup");
		vTaskDelete(NULL);
	}

	B_MQTTInit();
	
	QueueHandle_t mqttQueue = B_GetAddress(taskParameter->addressMap, B_TASKID_MQTT);
	while(true) {
		// Wait for messages from the queue
		B_command_t command = { 0 };
		if (xQueueReceive(mqttQueue, (void* const)&command, portMAX_DELAY) != pdTRUE) {
			ESP_LOGE(mqttTag, "Failed to receive command from queue, discarding it");
			continue;
		}

		ESP_LOGI(mqttTag, "Received command from queue: from=%u, dest=%u, header=%u", command.from, command.dest, command.header);

			// Process the command here
			// For example, you could send a response or handle the command
			B_SendMQTTBinary(mqttClient);
	}

	// Task paniced, clean up and delete task
	B_MQTTCleanup();
	vTaskDelete(NULL);
}

void B_MQTTCleanup()
{
	ESP_ERROR_CHECK(esp_mqtt_client_stop(mqttClient));
	ESP_ERROR_CHECK(esp_mqtt_client_destroy(mqttClient));
}
