#include "B_mqtt.h"

static const char* mqttTag = "BarnaNet - MQTT";
static B_addressMap_t* addressMapPointer = NULL; // If the MQTT handler was an ISR, this would not work (it would need to be passed into it)
static esp_mqtt_client_handle_t mqttClient;

// Intermediate function to relay a command received from MQTT to the appropriate task
// Sanitizes the command
// - Private function
static void B_MQTTRelayCommand(B_command_t* const command, uint8_t transmissionID)
{
	// Sanitize request type
	if (B_COMMAND_OP(command->header) == B_COMMAND_OP_RES || B_COMMAND_OP(command->header) == B_COMMAND_OP_ERR) {
		ESP_LOGW(mqttTag, "Invalid request type");
		B_SendStatusReply(addressMapPointer, command, B_TASKID_MQTT, B_COMMAND_OP_ERR, "Invalid request type");
		return;
	}

	if (!B_RelayCommand(addressMapPointer, command, B_TASKID_MQTT, transmissionID)) {
		ESP_LOGE(mqttTag, "Failed to relay command");
		B_SendStatusReply(addressMapPointer, command, B_TASKID_MQTT, B_COMMAND_OP_ERR, "INTERNAL: Relay error");
		return;
	}
}

// Handles incoming C2D messages
// Called by B_RouteMQTTMessage
// The data is in raw bytes as we need it
// - Private function
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

	B_MQTTRelayCommand(&command, 0); // 0 as transmissionID, will be ignored
}

// Handles incoming Direct Method invocations
// Called by B_RouteMQTTMessage
// The data is a JSON array of bytes representing the command struct
// - Private function
static void B_HandleDirectMethod(esp_mqtt_client_handle_t client, int topicLen, char* topic, int dataLen, char* data)
{
	ESP_LOGI(mqttTag, "Handling Direct Method");

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
	ESP_LOGI(mqttTag, "Query buffer: %s", queryBuffer);

	// TODO: string length checks

	// Extract the $rid parameter from the query
	char ridBuffer[32] = { 0 };
	ESP_ERROR_CHECK(httpd_query_key_value(queryBuffer, "$rid", ridBuffer, sizeof(ridBuffer) / sizeof(char)));

	int rid = atoi(ridBuffer);
	ESP_LOGI(mqttTag, "$rid=%s (%i)", ridBuffer, rid);

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

	cJSON_Delete(root);
	ESP_LOGI(mqttTag, "Command from: %u, dest: %u, header: %u", command.from, command.dest, command.header);

	// Relay the command
	if (rid > 255) {
		ESP_LOGE(mqttTag, "We did not expect rid > 255");
	}

	B_MQTTRelayCommand(&command, rid % 256);
}

// Serializes the command into a JSON array and sends it as a Direct Method response
// The responseCommand's transmissionID field is used as the $rid in the topic
// - Private function
static void B_SendDirectMethodResponse(B_command_t* const responseCommand)
{
	// Serialize replyCommand
	cJSON *json_array = cJSON_CreateArray();
	if (!json_array)
		return;

	for (size_t i = 0; i < sizeof(B_command_t); ++i) {
		cJSON_AddItemToArray(json_array, cJSON_CreateNumber(((uint8_t*)responseCommand)[i]));
	}

	char *json_str = cJSON_PrintUnformatted(json_array); // You may use cJSON_Print for pretty print
	cJSON_Delete(json_array);

	// TODO: if the response code is error, send 500 instead of 200

	// Create the topic URI
	char replyTopic[64] = { 0 };
	strcpy(replyTopic, "$iothub/methods/res/200/?$rid=");

	char ridBuffer[16] = { 0 };
	itoa(responseCommand->transmissionID, ridBuffer, 10);
	
	if ((strlen(replyTopic) + strlen(ridBuffer) + 1) > sizeof(replyTopic)) {
		ESP_LOGE(mqttTag, "MQTT reply topic URI longer than expected");
		free(json_str);
		return;
	}

	strcat(replyTopic, ridBuffer);

	int msg_id = esp_mqtt_client_publish(mqttClient, replyTopic, json_str, strlen(json_str), 0, 0);
	ESP_LOGI(mqttTag, "MQTT reply sent with msg_id=%d", msg_id);

	free(json_str);
}

// Routes the incoming MQTT message to the appropriate handler based on the topic
// Called by B_MQTTHandler
// - Private function
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

// Test function to send a binary message to the broker
// - Private function
static void B_SendMQTTBinary(esp_mqtt_client_handle_t client)
{
	const char* response = "Hello back";
	int msg_id = esp_mqtt_client_publish(client, "/topic/binary", response, 11, 0, 0);
	ESP_LOGI(mqttTag, "binary sent with msg_id=%d", msg_id);
}

// This function is called by the MQTT client event loop
// It handles incoming MQTT events and dispatches them to the appropriate handlers
// - handlerArgs user data registered to the event.
// - base Event base for the handler (always MQTT Base in this example).
// - eventID The id for the received event.
// - eventData The data for the event, esp_mqtt_event_handle_t.
// - Private function
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
				ESP_LOGI(mqttTag, "Last tls stack error rid: 0x%x", event->error_handle->esp_tls_stack_err);
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

// Initialize the MQTT client and initiates connecting to the broker
// - Private function
static void B_MQTTInit(B_addressMap_t* addressMap)
{
	// It is easier to use the address map as a global variable than pass it into the event handler
	addressMapPointer = addressMap;

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
			.verification.skip_cert_common_name_check = false // Do not verify the server certificate chain
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

// The MQTT task function
// Expects the B_MQTTTaskParameter struct
void B_MQTTTask(void* pvParameters)
{
	const struct B_MQTTTaskParameter* const taskParameter = (const struct B_MQTTTaskParameter* const)pvParameters;
	if (taskParameter == NULL || taskParameter->addressMap == NULL) {
		ESP_LOGE(mqttTag, "The MQTT task parameter is invalid, aborting startup");
		vTaskDelete(NULL);
	}

	B_MQTTInit(taskParameter->addressMap);

	// Get the MQTT queue
	QueueHandle_t mqttQueue = B_GetAddress(taskParameter->addressMap, B_TASKID_MQTT);
	if (mqttQueue == B_ADDRESS_MAP_INVALID_QUEUE) {
		ESP_LOGE(mqttTag, "The MQTT queue is invalid, aborting startup");
		vTaskDelete(NULL);
	}

	while(true) {
		// Wait for response commands from tasks
		B_command_t responseCommand = { 0 };
		if (xQueueReceive(mqttQueue, (void* const)&responseCommand, portMAX_DELAY) != pdTRUE) {
			ESP_LOGE(mqttTag, "Failed to receive command from queue, discarding it");
			continue;
		}

		ESP_LOGI(mqttTag, "Received command from queue: from=%u, dest=%u, header=%u, transmissionID=%u", responseCommand.from, responseCommand.dest, responseCommand.header, responseCommand.transmissionID);

		// C2D commands do not expect replies
		if (responseCommand.transmissionID == 0) {
			ESP_LOGW(mqttTag, "C2D commands do not expect replies, discarding command");
			continue;
		}

		// Send the Direct Method response back
		B_SendDirectMethodResponse(&responseCommand);
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
