#include "B_mqtt.h"

static const char* mqttTag = "BarnaNet - MQTT";
static const char* mqttTwinTag = "BarnaNet - MQTT Twin";
static const char* mqttEventLoopTag = "BarnaNet - MQTT Event Loop";


static B_addressMap_t* addressMapPointer = NULL; // If the MQTT handler was an ISR, this would not work (it would need to be passed into it)
static esp_mqtt_client_handle_t mqttClient;

static int requestIDCounter = 0; // Incrementing counter to generate unique request IDs

static QueueHandle_t twinQueue = NULL;
TickType_t twinReportInterval = portMAX_DELAY;
static struct B_MQTTTwinTaskParameter twinTaskParameter = { 0 };

static void B_MQTTTwinTask(void* pvParameters)
{
	const struct B_MQTTTwinTaskParameter* const taskParameter = (const struct B_MQTTTwinTaskParameter* const)pvParameters;
	if (taskParameter == NULL || taskParameter->addressMap == NULL) {
		ESP_LOGE(mqttTwinTag, "The MQTT Twin task parameter is invalid, aborting startup");
		vTaskDelete(NULL);
	}

	while (true) {
		
		// Wait for event or timeout
		B_twinEvent_t event;
		if (xQueueReceive(twinQueue, &event, twinReportInterval) == pdTRUE) {

			switch (event.type) {
				case B_TWIN_EVENT_SYNCHRONIZATION:
					ESP_LOGI(mqttTwinTag, "Twin Synchronization Event");

					if (taskParameter->SynchronizationCallback != NULL) {
						taskParameter->SynchronizationCallback(event.payload, event.payloadLen);
					}

					// We own this memory now, we must free it
					free(event.payload);
					break;

				case B_TWIN_EVENT_PROCESS_DESIRED:
					ESP_LOGI(mqttTwinTag, "Processing Desired Properties...");

					if (taskParameter->DesiredStateCallback != NULL) {
						taskParameter->DesiredStateCallback(event.payload, event.payloadLen);
					}

					// We own this memory now, we must free it
					free(event.payload);
					break;

				case B_TWIN_EVENT_SEND_REPORTED:
					ESP_LOGI(mqttTwinTag, "Manual Report Triggered");
					B_twinEvent_t reportedEvent = { 0 };

					// If the payload is set in the event, use it as the reported state to send, otherwise call the ReportedStateCallback to get the reported state to send
					if (event.payload != NULL) {
						reportedEvent.payload = event.payload;
						reportedEvent.payloadLen = event.payloadLen;
					}
					else {
						if (taskParameter->ReportedStateCallback == NULL)
							break;

						reportedEvent = taskParameter->ReportedStateCallback();
						if (reportedEvent.payload == NULL)
							break;
					}

					// Send the reported state to the broker
					char reportTopic[64] = { 0 };
					requestIDCounter++;
					sprintf(reportTopic, B_MQTT_TWIN_PATCH_REPORTED_TOPIC, requestIDCounter);
					
					int publishMessageID = esp_mqtt_client_publish(mqttClient, reportTopic, reportedEvent.payload, reportedEvent.payloadLen, 0, 0);
					ESP_LOGI(mqttTwinTag, "Sent reported state with message ID=%d", publishMessageID);

					// We own this memory now, we must free it
					free(reportedEvent.payload);
					break;
			}
		}
		else {
			// Timeout occurred -> Time to send periodic report
			ESP_LOGI(mqttTwinTag, "Periodic Report Triggered");
			if (taskParameter->ReportedStateCallback == NULL)
				continue;

			B_twinEvent_t reportedEvent = taskParameter->ReportedStateCallback();
			if (reportedEvent.payload == NULL)
				continue;

			char reportTopic[64] = { 0 };
			requestIDCounter++;
			sprintf(reportTopic, B_MQTT_TWIN_PATCH_REPORTED_TOPIC, requestIDCounter);
			
			int publishMessageID = esp_mqtt_client_publish(mqttClient, reportTopic, reportedEvent.payload, reportedEvent.payloadLen, 0, 0);
			ESP_LOGI(mqttTwinTag, "Sent reported state with message ID=%d", publishMessageID);

			free(reportedEvent.payload);
		}
	}
}

// Intermediate function to relay a command received from MQTT to the appropriate task
// Sanitizes the command
// - Private function
static void B_MQTTRelayCommand(B_command_t* const command, uint8_t transmissionID)
{
	// Sanitize request type
	if (B_COMMAND_OP(command->header) == B_COMMAND_OP_RES || B_COMMAND_OP(command->header) == B_COMMAND_OP_ERR) {
		ESP_LOGW(mqttEventLoopTag, "Invalid request type");
		B_SendStatusReply(addressMapPointer, command, B_TASKID_MQTT, B_COMMAND_OP_ERR, "Invalid request type");
		return;
	}

	if (!B_RelayCommand(addressMapPointer, command, B_TASKID_MQTT, transmissionID)) {
		ESP_LOGE(mqttEventLoopTag, "Failed to relay command");
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
	ESP_LOGI(mqttEventLoopTag, "Received C2D message");

	if (dataLen <= 0 || dataLen > B_COMMAND_STRUCT_SIZE) {
		ESP_LOGE(mqttEventLoopTag, "Invalid data received");
		return;
	}

	// The data should be in raw bytes as we need it
	B_command_t command = { 0 };
	memcpy((void*)&command, data, dataLen);
	ESP_LOGI(mqttEventLoopTag, "Command from: %u, dest: %u, header: %u", command.from, command.dest, command.header);

	B_MQTTRelayCommand(&command, 0); // 0 as transmissionID, will be ignored
}

// Handles incoming Direct Method invocations
// Called by B_RouteMQTTMessage
// The data is a JSON array of bytes representing the command struct
// - Private function
static void B_HandleDirectMethod(esp_mqtt_client_handle_t client, int topicLen, char* topic, int dataLen, char* data)
{
	ESP_LOGI(mqttEventLoopTag, "Handling Direct Method");

	// Find the query start in the URI
	char* queryStart = strstr(topic, "?");
	if (!queryStart) {
		ESP_LOGE(mqttEventLoopTag, "Query part not found");
		return;
	}
	queryStart += 1; // Exclude the questionmark
	int offset = queryStart - topic;

	char queryBuffer[512] = { 0 };
	memcpy(queryBuffer, queryStart, topicLen - offset);
	ESP_LOGI(mqttEventLoopTag, "Query buffer: %s", queryBuffer);

	// TODO: string length checks

	// Extract the $rid parameter from the query
	char ridBuffer[32] = { 0 };
	ESP_ERROR_CHECK(httpd_query_key_value(queryBuffer, "$rid", ridBuffer, sizeof(ridBuffer) / sizeof(char)));

	int rid = atoi(ridBuffer);
	ESP_LOGI(mqttEventLoopTag, "$rid=%s (%i)", ridBuffer, rid);

	// Use CJSON to deserialize the data, the JSON root should be an array
	cJSON* root = cJSON_Parse(data);
	if (!root) {
		ESP_LOGE(mqttEventLoopTag, "Failed to parse JSON");
		return;
	}

	if (!cJSON_IsArray(root)) {
		ESP_LOGE(mqttEventLoopTag, "Expected array");
		cJSON_Delete(root);
		return;
	}

	B_command_t command = { 0 };

	// Process the array elements
	cJSON* element = NULL;
	int elementIndex = 0;
	cJSON_ArrayForEach(element, root) {
		if (elementIndex >= B_COMMAND_STRUCT_SIZE) {
			ESP_LOGE(mqttEventLoopTag, "Array element index %d exceeds command body size", elementIndex);
			break;
		}

		if (!cJSON_IsNumber(element)) {
			ESP_LOGE(mqttEventLoopTag, "Expected number");
			continue;
		}

		if (element->valueint < 0 || element->valueint > 255) {
			ESP_LOGE(mqttEventLoopTag, "Array element value %d out of range", element->valueint);
			continue;
		}

		// TODO: This looks ugly
		((uint8_t*)&command)[elementIndex] = (uint8_t)element->valueint;
		elementIndex++;
	}

	cJSON_Delete(root);
	ESP_LOGI(mqttEventLoopTag, "Command from: %u, dest: %u, header: %u", command.from, command.dest, command.header);

	// Save the request ID in the command's transmissionID field, so that the response can use it to set the $rid in the topic
	uint8_t transmissionID = (uint8_t)rid;
	if (rid > 255) {
		// rid is never 0, but if it is above 255 that would be a problem
		ESP_LOGE(mqttEventLoopTag, "We did not expect rid > 255");
		transmissionID = 0;
	}

	// Relay the command
	B_MQTTRelayCommand(&command, transmissionID);
}

// Serializes the command into a JSON array and sends it as a Direct Method response
// The responseCommand's transmissionID field is used as the $rid in the topic
// - Private function
static void B_SendDirectMethodResponse(B_command_t* const responseCommand)
{
	// Serialize replyCommand
	cJSON *jsonArray = cJSON_CreateArray();
	if (!jsonArray)
		return;

	for (size_t i = 0; i < sizeof(B_command_t); ++i) {
		cJSON_AddItemToArray(jsonArray, cJSON_CreateNumber(((uint8_t*)responseCommand)[i]));
	}

	char *jsonStringified = cJSON_PrintUnformatted(jsonArray); // Use cJSON_Print for pretty print
	cJSON_Delete(jsonArray);

	// TODO: if the response code is error, send 500 instead of 200

	// Create the topic URI
	char replyTopic[64] = { 0 };
	strcpy(replyTopic, "$iothub/methods/res/200/?$rid=");

	// Get the rid from the responseCommand's transmissionID field and append it to the topic
	char ridBuffer[16] = { 0 };
	itoa(responseCommand->transmissionID, ridBuffer, 10);

	// Check if the topic URI is long enough to hold the rid
	if ((strlen(replyTopic) + strlen(ridBuffer) + 1) > sizeof(replyTopic)) {
		ESP_LOGE(mqttTag, "MQTT reply topic URI longer than expected");
		free(jsonStringified);
		return;
	}

	strcat(replyTopic, ridBuffer);

	int publishMessageID = esp_mqtt_client_publish(mqttClient, replyTopic, jsonStringified, strlen(jsonStringified), 0, 0);
	ESP_LOGI(mqttTag, "MQTT reply sent with message ID=%d", publishMessageID);

	free(jsonStringified);
}

// Routes the incoming MQTT message to the appropriate handler based on the topic
// Called by B_MQTTHandler
// - Private function
static void B_RouteMQTTMessage(esp_mqtt_client_handle_t client, int topicLen, char* topic, int dataLen, char* data)
{
	// Route the topic to the appropriate handler (remove the # and the null terminator from the end)
	if (strncmp(topic, B_MQTT_C2D_TOPIC, sizeof(B_MQTT_C2D_TOPIC) - 2) == 0) {
		B_HandleC2DMessage(client, topicLen, topic, dataLen, data);
	}
	else if (strncmp(topic, B_MQTT_DIRECT_METHOD_TOPIC, sizeof(B_MQTT_DIRECT_METHOD_TOPIC) - 2) == 0) {
		B_HandleDirectMethod(client, topicLen, topic, dataLen, data);
	}
	else if (strncmp(topic, B_MQTT_TWIN_TOPIC, sizeof(B_MQTT_TWIN_TOPIC) - 2) == 0) {

		// 200 is synchronization response, 204 is desired property update, other topics are ignored for now

		// Check status code
		if (strncmp(topic + sizeof(B_MQTT_TWIN_TOPIC) - 2, "429", 3) == 0) {
			ESP_LOGE(mqttEventLoopTag, "Twin synchronization response received error, Too many requests (throttled)");
			return;
		}

		if (strncmp(topic + sizeof(B_MQTT_TWIN_TOPIC) - 2, "5", 1) == 0) {
			ESP_LOGE(mqttEventLoopTag, "Twin synchronization response received server error");
			return;
		}

		// 204 responses have no payload, we can ignore them
		if (dataLen == 0) {
			return;
		}

		// Forward the twin message to the twin task via the queue
		// TODO: Removes the request ID parameter. Could it be usefult downstream?
		B_twinEvent_t event = { 0 };

		// Ownership of the payload is transferred to the twin task, it must free it after processing
		event.payload = malloc(dataLen);
		if (event.payload == NULL) {
			ESP_LOGE(mqttEventLoopTag, "Failed to allocate memory for twin event payload");
			return;
		}

		memcpy(event.payload, data, dataLen);
		event.payloadLen = dataLen;
		event.type = B_TWIN_EVENT_SYNCHRONIZATION;
		
		if (xQueueSend(twinQueue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
			ESP_LOGE(mqttEventLoopTag, "Failed to send twin event to queue");
			free(event.payload);
			return;
		}
	}
	else if (strncmp(topic, B_MQTT_TWIN_RECEIVE_DESIRED_TOPIC, sizeof(B_MQTT_TWIN_RECEIVE_DESIRED_TOPIC) - 1) == 0) {
		// This is a desired property update, forward it to the twin task via the queue
		B_twinEvent_t event = { 0 };

		// Ownership of the payload is transferred to the twin task, it must free it after processing
		event.payload = malloc(dataLen);
		if (event.payload == NULL) {
			ESP_LOGE(mqttEventLoopTag, "Failed to allocate memory for twin event payload");
			return;
		}

		memcpy(event.payload, data, dataLen);
		event.payloadLen = dataLen;
		event.type = B_TWIN_EVENT_PROCESS_DESIRED;
		
		if (xQueueSend(twinQueue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
			ESP_LOGE(mqttEventLoopTag, "Failed to send twin event to queue");
			free(event.payload);
			return;
		}
	}
	else {
		ESP_LOGW(mqttEventLoopTag, "Unhandled topic: %.*s", topicLen, topic);
	}
}

// Test function to send a binary message to the broker
// - Private function
static void B_SendMQTTBinary(esp_mqtt_client_handle_t client)
{
	const char* response = "Hello back";
	int publishMessageID = esp_mqtt_client_publish(client, "/topic/binary", response, 11, 0, 0);
	ESP_LOGI(mqttTag, "binary sent with message ID=%d", publishMessageID);
}

// This function is called by the MQTT client event loop
// It handles incoming MQTT events and dispatches them to the appropriate handlers
// - handlerArgs user data registered to the event.
// - base Event base for the handler (always MQTT Base in this example).
// - eventID The id for the received event.
// - eventData The data for the event, esp_mqtt_event_handle_t.
// - Private function
static int twinTopicSubscriptionMessageID = 0; // We need to track this to know when the twin topic subscription is acknowledged so we can trigger the initial synchronization
static void B_MQTTHandler(void* handlerArgs, esp_event_base_t base, int32_t eventID, void* eventData)
{
	ESP_LOGD(mqttEventLoopTag, "Event dispatched from event loop base=%s, eventID=%" PRIi32, base, eventID);
	esp_mqtt_event_handle_t event = eventData;
	esp_mqtt_client_handle_t client = event->client;
	int messageID = 0;

	switch ((esp_mqtt_event_id_t)eventID) {
		case MQTT_EVENT_CONNECTED:
			ESP_LOGI(mqttEventLoopTag, "MQTT_EVENT_CONNECTED");

			// Subscribe to topics
			twinTopicSubscriptionMessageID = esp_mqtt_client_subscribe(client, B_MQTT_TWIN_TOPIC, 0);
			ESP_LOGI(mqttTwinTag, "Sent subscribe request to %s, message ID=%d", B_MQTT_TWIN_TOPIC, twinTopicSubscriptionMessageID);

			messageID = esp_mqtt_client_subscribe(client, B_MQTT_TWIN_PATCH_TOPIC, 0);
			ESP_LOGI(mqttTwinTag, "Sent subscribe request to %s, message ID=%d", B_MQTT_TWIN_PATCH_TOPIC, messageID);
			
			messageID = esp_mqtt_client_subscribe(client, B_MQTT_C2D_TOPIC, 0);
			ESP_LOGI(mqttTwinTag, "Sent subscribe request to %s, message ID=%d", B_MQTT_C2D_TOPIC, messageID);

			messageID = esp_mqtt_client_subscribe(client, B_MQTT_DIRECT_METHOD_TOPIC, 0);
			ESP_LOGI(mqttEventLoopTag, "Sent subscribe request to %s, message ID=%d", B_MQTT_DIRECT_METHOD_TOPIC, messageID);
			break;

		case MQTT_EVENT_DISCONNECTED:
			ESP_LOGI(mqttEventLoopTag, "MQTT_EVENT_DISCONNECTED");
			break;

		case MQTT_EVENT_SUBSCRIBED:
			ESP_LOGI(mqttEventLoopTag, "MQTT_EVENT_SUBSCRIBED, message ID=%d", event->msg_id);

			// After subscribing, we can trigger the initial synchronization by sending a request to the special topic
			// Check if this is the subscribe event for the twin topic
			if (event->msg_id == twinTopicSubscriptionMessageID) {
				char retrieveTopic[64] = { 0 };
				requestIDCounter++;
				sprintf(retrieveTopic, B_MQTT_TWIN_RETRIEVE_TOPIC, requestIDCounter);

				int publishMessageID = esp_mqtt_client_publish(client, retrieveTopic, "", 0, 0, 0);
				ESP_LOGI(mqttEventLoopTag, "Sent twin retrieve request with message ID=%d", publishMessageID);
			}

			break;

		case MQTT_EVENT_UNSUBSCRIBED:
			ESP_LOGI(mqttEventLoopTag, "MQTT_EVENT_UNSUBSCRIBED, message ID=%d", event->msg_id);
			break;

		case MQTT_EVENT_PUBLISHED:
			ESP_LOGI(mqttEventLoopTag, "MQTT_EVENT_PUBLISHED, message ID=%d", event->msg_id);
			break;

		case MQTT_EVENT_DATA:
			ESP_LOGI(mqttEventLoopTag, "MQTT_EVENT_DATA");
			printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
			printf("DATA=%.*s\r\n", event->data_len, event->data);

			B_RouteMQTTMessage(client, event->topic_len, event->topic, event->data_len, event->data);
			break;

		case MQTT_EVENT_ERROR:
			ESP_LOGI(mqttEventLoopTag, "MQTT_EVENT_ERROR");
			if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
				ESP_LOGI(mqttEventLoopTag, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
				ESP_LOGI(mqttEventLoopTag, "Last tls stack error rid: 0x%x", event->error_handle->esp_tls_stack_err);
				ESP_LOGI(mqttEventLoopTag, "Last captured errno: %d (%s)", event->error_handle->esp_transport_sock_errno,
					strerror(event->error_handle->esp_transport_sock_errno));
			}
			else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
				ESP_LOGI(mqttEventLoopTag, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
			}
			else {
				ESP_LOGW(mqttEventLoopTag, "Unknown error type: 0x%x", event->error_handle->error_type);
			}
			break;

		default:
			ESP_LOGI(mqttEventLoopTag, "Other event id: %d", event->event_id);
			break;
	}
}

// Initialize the MQTT client and initiates connecting to the broker
// - Private function
static void B_MQTTInit(B_addressMap_t* addressMap, const char* verificationCertificate, const char* authenticationCertificate, const char* authenticationKey, size_t twinTaskStackSize, void (*SynchronizationCallback)(const char* json, size_t len), void (*DesiredStateCallback)(const char* json, size_t len), B_twinEvent_t (*ReportedStateCallback)())
{
	// It is easier to use the address map as a global variable than pass it into the event handler
	addressMapPointer = addressMap;
	
	// Create the twin task and its queue
	twinQueue = xQueueCreate(B_MQTT_TWIN_QUEUE_LENGTH, sizeof(B_twinEvent_t));
	if (twinQueue == NULL) {
		ESP_LOGE(mqttTag, "Failed to create MQTT Twin queue");
		return;
	}

	if (twinTaskStackSize == 0){
		ESP_LOGW(mqttTag, "Twin task stack size is incorrect, using default value of %d", B_MQTT_TWIN_TASK_DEFAULT_STACK_SIZE);
		twinTaskStackSize = B_MQTT_TWIN_TASK_DEFAULT_STACK_SIZE;
	}

	twinTaskParameter.addressMap = addressMap;
	twinTaskParameter.SynchronizationCallback = SynchronizationCallback;
	twinTaskParameter.DesiredStateCallback = DesiredStateCallback;
	twinTaskParameter.ReportedStateCallback = ReportedStateCallback;
	xTaskCreate(B_MQTTTwinTask, "B_MQTTTwinTask", twinTaskStackSize, &twinTaskParameter, 3, NULL);

	ESP_LOGI(mqttTag, "Initialized MQTT Twin Task");

	
	// Initialize MQTT client
	const esp_mqtt_client_config_t mqttConfig = {
		.broker = {
			//.address.uri = B_MQTT_B_URL, // This line could replace the hostname, port and transport
			.address.hostname = B_MQTT_HOST,
			.address.port = B_MQTT_PORT,
			.address.transport = MQTT_TRANSPORT_OVER_SSL,
			.verification.certificate = verificationCertificate,
			//.verification.certificate = (const char*)_binary_BarnaNet_CA_crt_start,
			//.verification.use_global_ca_store = false,
			.verification.skip_cert_common_name_check = false // Do not verify the server certificate chain
		},
		.credentials = {
			.username = B_MQTT_USERNAME,
			//.authentication.password = B_AZURE_SAS,
			.client_id = CONFIG_B_MQTT_DEVICE_ID,
			.authentication.certificate = authenticationCertificate,
			.authentication.key = authenticationKey
		}
	};
	mqttClient = esp_mqtt_client_init(&mqttConfig);

	// The last argument may be used to pass data to the event handler
	ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqttClient, ESP_EVENT_ANY_ID, B_MQTTHandler, NULL));
	ESP_ERROR_CHECK(esp_mqtt_client_start(mqttClient));

	ESP_LOGI(mqttTag, "Initialized MQTT client");
}

// The MQTT task function
// Expects the B_MQTTTaskParameter struct
void B_MQTTTask(void* pvParameters)
{
	const struct B_MQTTTaskParameter* const taskParameter = (const struct B_MQTTTaskParameter* const)pvParameters;
	if (taskParameter == NULL || taskParameter->addressMap == NULL || taskParameter->verificationCertificate == NULL || taskParameter->authenticationCertificate == NULL || taskParameter->authenticationKey == NULL) {
		ESP_LOGE(mqttTag, "The MQTT task parameter is invalid, aborting startup");
		vTaskDelete(NULL);
	}

	B_MQTTInit(
		taskParameter->addressMap,
		taskParameter->verificationCertificate,
		taskParameter->authenticationCertificate,
		taskParameter->authenticationKey,
		taskParameter->twinTaskStackSize,
		taskParameter->SynchronizationCallback,
		taskParameter->DesiredStateCallback,
		taskParameter->ReportedStateCallback
	);

	// Get the MQTT queue
	QueueHandle_t mqttQueue = B_GetAddress(taskParameter->addressMap, B_TASKID_MQTT);
	if (mqttQueue == B_ADDRESS_MAP_INVALID_QUEUE) {
		ESP_LOGE(mqttTag, "The MQTT queue is invalid, aborting startup");
		vTaskDelete(NULL);
	}

	while (true) {
		// Wait for response commands from tasks
		B_command_t responseCommand = { 0 };
		if (xQueueReceive(mqttQueue, (void* const)&responseCommand, portMAX_DELAY) != pdTRUE) {
			ESP_LOGE(mqttTag, "Failed to receive command from queue, discarding it");
			continue;
		}

		uint8_t commandOP = B_COMMAND_OP(responseCommand.header);
		uint8_t commandID = B_COMMAND_ID(responseCommand.header);

		ESP_LOGI(mqttTag, "Received command from queue: from=%u, dest=%u, header=%u, transmissionID=%u", responseCommand.from, responseCommand.dest, responseCommand.header, responseCommand.transmissionID);

		// Response to a C2D command
		if ((commandOP == B_COMMAND_OP_RES || commandOP == B_COMMAND_OP_ERR) && responseCommand.transmissionID == 0) {
			// C2D commands do not expect replies
			ESP_LOGW(mqttTag, "C2D commands do not expect replies, discarding command");
		}

		// Response to a Direct Method
		else if ((commandOP == B_COMMAND_OP_RES || commandOP == B_COMMAND_OP_ERR) && responseCommand.transmissionID != 0) {
			B_SendDirectMethodResponse(&responseCommand);
		}

		// Read the reported state from the command body and send it to the broker
		else if (commandOP == B_COMMAND_OP_SET && commandID == B_MQTT_COMMAND_SEND_REPORTED) {

			int reportedStateSize = strnlen((char*)responseCommand.body, B_COMMAND_BODY_SIZE);
			char* reportedStateBuffer = (char*)malloc(reportedStateSize);
			if (reportedStateBuffer == NULL) {
				ESP_LOGE(mqttTag, "Failed to allocate memory for reported state buffer");
				continue;
			}

			memcpy((void*)reportedStateBuffer, responseCommand.body, reportedStateSize);

			// Report command
			B_twinEvent_t triggerReportedEvent = { 0 };
			triggerReportedEvent.type = B_TWIN_EVENT_SEND_REPORTED;
			triggerReportedEvent.payload = reportedStateBuffer;
			triggerReportedEvent.payloadLen = reportedStateSize;
			
			if (xQueueSend(twinQueue, &triggerReportedEvent, pdMS_TO_TICKS(100)) != pdTRUE) {
				ESP_LOGE(mqttTag, "Failed to send trigger reported event to queue");
				continue;
			}
		}

		// Send a trigger to the twin task to compile the reported state and send it to the broker
		else if (commandOP == B_COMMAND_OP_SET && commandID == B_MQTT_COMMAND_COMPILE_AND_SEND_REPORTED) {
			// Report command
			B_twinEvent_t triggerReportedEvent = { 0 };
			triggerReportedEvent.type = B_TWIN_EVENT_SEND_REPORTED;
			
			if (xQueueSend(twinQueue, &triggerReportedEvent, pdMS_TO_TICKS(100)) != pdTRUE) {
				ESP_LOGE(mqttTag, "Failed to send trigger reported event to queue");
				continue;
			}
		}

		else if (commandOP == B_COMMAND_OP_SET && commandID == B_MQTT_COMMAND_SET_REPORT_INTERVAL) {
			uint32_t newInterval = B_ReadCommandBody_DWORD(&responseCommand, 0);

			if (newInterval == 0) {
				twinReportInterval = portMAX_DELAY;
				ESP_LOGI(mqttTag, "Turned off periodic reporting");
			}
			else if (newInterval < 30000) {
				newInterval = 30000;
				ESP_LOGW(mqttTag, "Report interval cannot be set below 30 seconds, setting it to 30 seconds");
			}
			
			twinReportInterval = pdMS_TO_TICKS(newInterval);
		}

		else {
			ESP_LOGW(mqttTag, "Invalid command received in MQTT task, discarding it");
		}
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
