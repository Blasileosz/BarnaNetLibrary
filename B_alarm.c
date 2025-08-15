#include "B_alarm.h"

static const char* alarmTag = "BarnaNet - Alarm";

static struct B_AlarmContainer alarmContainer = {.buffer = NULL, .size = 0};
static int selectedAlarmIndex = -1;
static gptimer_handle_t alarmTimer;

// ISR function must comply with the gptimer_alarm_cb_t definition
static bool IRAM_ATTR B_AlarmInterrupt(gptimer_handle_t timer, const gptimer_alarm_event_data_t* eventData, void* userData)
{
	BaseType_t highPriorityTaskWoken = pdFALSE;
	QueueHandle_t queue = (QueueHandle_t)userData;

	B_command_t sendCommand = { 
		.from = B_TASKID_ALARM,
		.dest = B_TASKID_ALARM,
		.header = B_COMMAND_OP_RES | B_ALARM_COMMAND_TRIGGER
	};

	// Cannot ESP_ERROR_CHECK in ISR
	xQueueSendFromISR(queue, &sendCommand, &highPriorityTaskWoken);

	return highPriorityTaskWoken == pdTRUE;
}

static void B_PrintNextTriggerDelta(int returnValue)
{
	ESP_LOGI(alarmTag, "%i days, %02i:%02i:%02i", (returnValue / (24 * 3600)), B_TimepartGetHours(returnValue), B_TimepartGetMinutes(returnValue), B_TimepartGetSeconds(returnValue));
}

static B_timepart_t B_AlarmFindNextTrigger(const B_AlarmInfo_t* const alarm, const struct tm* const localTime)
{
	int wday = localTime->tm_wday;
	int timePartNow = B_BuildTimepart(localTime->tm_hour, localTime->tm_min, localTime->tm_sec);
	B_timepart_t alarmTrigTime = alarm->localTimepart;

	// Day of month: tm_mday values: [1, 31]
	// Month of year: tm_mon values: [0, 11]
	if (alarmTrigTime == B_ALARM_TRIGGER_SUNRISE)
		alarmTrigTime = B_GetLocalTimepart(B_SUNRISE_TABLE[localTime->tm_mon][localTime->tm_mday - 1]);
	else if (alarmTrigTime == B_ALARM_TRIGGER_SUNSET)
		alarmTrigTime = B_GetLocalTimepart(B_SUNSET_TABLE[localTime->tm_mon][localTime->tm_mday - 1]);

	// If it has triggered today, skip to tomorrow
	bool isOffsetDay = alarmTrigTime <= timePartNow && alarm->days & (1 << wday);

	for (int i = 0; i < 7; i++) {
		int day = (i + wday + (int)isOffsetDay) % 7; // Circular search
		
		if (alarm->days & (1 << day)) {
			//printf("Next occurence: %u\n", i);
			return alarmTrigTime - timePartNow + ((i + (int)isOffsetDay) * 24 * 3600);
		}
	}

	// Error, likely alarm->days == 0
	return 0xFFFFFFFF;
}

static B_timepart_t B_AlarmFindNextTrigger2(const B_AlarmInfo_t* const alarm, const struct tm* const localTime)
{
	int wday = localTime->tm_wday;
	int timePartNow = B_BuildTimepart(localTime->tm_hour, localTime->tm_min, localTime->tm_sec);
	B_timepart_t alarmTrigTime = alarm->localTimepart;

	if (alarmTrigTime == B_ALARM_TRIGGER_SUNRISE)
		alarmTrigTime = B_GetLocalTimepart(B_SUNRISE_TABLE[localTime->tm_mon][localTime->tm_mday - 1]);
	else if (alarmTrigTime == B_ALARM_TRIGGER_SUNSET)
		alarmTrigTime = B_GetLocalTimepart(B_SUNRISE_TABLE[localTime->tm_mon][localTime->tm_mday - 1]);

	// If it has triggered today, skip to tomorrow
	bool isOffsetDay = alarmTrigTime <= timePartNow && alarm->days & (1 << wday);

	// Put two weeks of days after each other to simulate circular search
	uint16_t twoWeeks = (uint16_t)alarm->days << 7;
	twoWeeks |= alarm->days;
	twoWeeks >>= wday + (uint16_t)isOffsetDay; // Discard the passed week + today if it is passed

	// Count trailing zeros (starting from least significant bit)
	// Find the first occurence of a one starting from least significant bit
	uint16_t nextDay = twoWeeks & -twoWeeks;
	nextDay <<= (uint16_t)isOffsetDay; // Add back today if passed
	uint16_t trailingZeros = (uint16_t)log2(nextDay);

	//printf("Two weeks: %u\n", twoWeeks);
	//printf("Next occurence: %u %u\n", nextDay, trailingZeros);

	return alarmTrigTime - timePartNow + (trailingZeros * 24 * 3600);
}

static B_timepart_t B_FindNextAlarm(int* outAlarmIndex)
{
	time_t now = 0; // UTC time
	time(&now);
	struct tm timeStruct = { 0 }; // Local time
	localtime_r(&now, &timeStruct);

	B_timepart_t minTrigTime = 0xFFFFFFFF;
	int closestAlarmIndex = -1;
	for (int i = 0; i < alarmContainer.size; i++)
	{
		int trigTime = B_AlarmFindNextTrigger(&alarmContainer.buffer[i], &timeStruct);
		B_PrintNextTriggerDelta(trigTime);
		if (trigTime < minTrigTime) {
			minTrigTime = trigTime;
			closestAlarmIndex = i;
		}
	}

	// If the alarm container is empty, returns the default values (-1 and 0xFFFFFFFF)
	*outAlarmIndex = closestAlarmIndex;
	return minTrigTime;
}

static void B_LoadAlarmsFromNVS()
{
	nvs_handle_t nvsHandle;
	esp_err_t err = nvs_open(B_ALARM_NVS_NAMESPACE, NVS_READONLY, &nvsHandle);

	// Namespace not yet initialized, it does by saving a value to it
	if (err == ESP_ERR_NVS_NOT_FOUND) {
		ESP_LOGI(alarmTag, "NVS namespace not yet initialized");
		return;
	}

	// Read stored alarm container size
	uint8_t containerSize = 0; // Alarm count
	 err = nvs_get_u8(nvsHandle, B_ALARM_NVS_CONTAINER_SIZE, &containerSize);
	if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
		ESP_ERROR_CHECK(err);

	if (containerSize == 0) {
		ESP_LOGI(alarmTag, "No alarms stored in NVS");
		nvs_close(nvsHandle);
		return;
	}
		
	// Read the stored buffer size in bytes
	size_t requiredBufferSize = 0; // Size in bytes
	err = nvs_get_blob(nvsHandle, B_ALARM_NVS_BUFFER, NULL, &requiredBufferSize);
	if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
		ESP_ERROR_CHECK(err);

	// Check if the buffer size aligns with the B_AlarmInfo_t structure size
	if (requiredBufferSize % sizeof(B_AlarmInfo_t) != 0) {
		// Structure does not align, probably changed, do not load
		ESP_LOGW(alarmTag, "B_AlarmInfo_t struct and buffer size stored in NVS do not align, erasing flash");
		nvs_flash_erase();
		nvs_close(nvsHandle);
		return;
	}

	// Check if the capacity changed 
	if (CONFIG_B_ALARM_CONTAINER_CAPACITY < containerSize) {
		ESP_LOGW(alarmTag, "Alarm container capacity config value lowered");
		requiredBufferSize = CONFIG_B_ALARM_CONTAINER_CAPACITY * sizeof(B_AlarmInfo_t);
	}

	err = nvs_get_blob(nvsHandle, B_ALARM_NVS_BUFFER, alarmContainer.buffer, &requiredBufferSize);
	if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
		ESP_ERROR_CHECK(err);

	alarmContainer.size = containerSize;

	nvs_close(nvsHandle);

	ESP_LOGI(alarmTag, "Alarms loaded from NVS");
}

static void B_SaveAlarmsToNVS()
{
	nvs_handle_t nvsHandle;
	ESP_ERROR_CHECK(nvs_open(B_ALARM_NVS_NAMESPACE, NVS_READWRITE, &nvsHandle));

	ESP_ERROR_CHECK(nvs_set_u8(nvsHandle, B_ALARM_NVS_CONTAINER_SIZE, alarmContainer.size));
	ESP_ERROR_CHECK(nvs_set_blob(nvsHandle, B_ALARM_NVS_BUFFER, (const void*)alarmContainer.buffer, B_ALARM_CONTAINER_BUFFER_SIZE));

	ESP_ERROR_CHECK(nvs_commit(nvsHandle));
	nvs_close(nvsHandle);

	ESP_LOGI(alarmTag, "Alarms saved to NVS");
}

static bool B_InsertAlarm(struct B_AlarmContainer* const container, B_timepart_t localTimepart, uint8_t days, const B_command_t* const triggerCommand, size_t commandSize)
{
	if (container->buffer == NULL || container->size >= CONFIG_B_ALARM_CONTAINER_CAPACITY) {
		ESP_LOGI(alarmTag, "Failed to insert alarm into container");
		return false;
	}

	container->buffer[container->size].localTimepart = localTimepart;
	container->buffer[container->size].days = days;
	memset(&container->buffer[container->size].triggerCommand, 0, sizeof(B_command_t)); // Clear buffer just in case
	memcpy(&container->buffer[container->size].triggerCommand, triggerCommand, commandSize);
	container->size++;

	B_SaveAlarmsToNVS();

	return true;
}

static bool B_RemoveAlarm(struct B_AlarmContainer* const container, uint8_t index)
{
	if (container->buffer == NULL || index >= container->size) {
		ESP_LOGI(alarmTag, "Failed to remove alarm from container");
		return false;
	}
	
	container->size--;
	if (container->size == 0)
		return true;

	// Copy from back of the buffer to the index
	container->buffer[index].localTimepart = container->buffer[container->size].localTimepart;
	container->buffer[index].days = container->buffer[container->size].days;
	memcpy(&container->buffer[index].triggerCommand, &container->buffer[container->size].triggerCommand, sizeof(B_command_t));

	B_SaveAlarmsToNVS();

	return true;
}

static bool B_TimerInit(QueueHandle_t alarmCommandQueue)
{
	// Allocate alarm container (memory is zero initialized at insert time)
	alarmContainer.buffer = malloc(B_ALARM_CONTAINER_BUFFER_SIZE);
	if (alarmContainer.buffer == NULL)
		return false;

	// Load saved alarms from NVS
	B_LoadAlarmsFromNVS();

	// Create timer
	// The internal counter value defaults to zero
	gptimer_config_t timerConfig = {
		.clk_src = GPTIMER_CLK_SRC_DEFAULT,
		.direction = GPTIMER_COUNT_UP,
		.resolution_hz = B_ALARM_TIMER_RESOLUTION_HZ
	};
	ESP_ERROR_CHECK(gptimer_new_timer(&timerConfig, &alarmTimer));

	// SOC_TIMER_GROUP_COUNTER_BIT_WIDTH shows the width of the counter

	// Get the next alarm for the timer to be started with
	// If the alarm container is empty, set the timer's counter to 0, from this RestartTimer will know that it has to start it
	B_timepart_t trigTime = B_FindNextAlarm(&selectedAlarmIndex);
	if (selectedAlarmIndex == -1){
		trigTime = 0;
		ESP_LOGI(alarmTag, "Empty alarm container at startup, timer wasn't started");
	}

	// Create alarm
	gptimer_alarm_config_t alarmConfig = {
		.alarm_count = (uint64_t)trigTime * B_ALARM_TIMER_RESOLUTION_HZ,
		.flags.auto_reload_on_alarm = false
	};
	ESP_ERROR_CHECK(gptimer_set_alarm_action(alarmTimer, &alarmConfig));

	// Register callback
	gptimer_event_callbacks_t callbackConfig = {
		.on_alarm = B_AlarmInterrupt
	};
	// Pass queue handle by value
	ESP_ERROR_CHECK(gptimer_register_event_callbacks(alarmTimer, &callbackConfig, alarmCommandQueue));

	// Enable timer
	ESP_ERROR_CHECK(gptimer_enable(alarmTimer));

	// Start timer if an alarm was selected, otherwise RestartTimer will start it later
	if (selectedAlarmIndex != -1)
		ESP_ERROR_CHECK(gptimer_start(alarmTimer));

	ESP_LOGI(alarmTag, "Timer configured");
	return true;
}

static void B_RestartTimer(B_timepart_t seconds)
{
	uint64_t timerValue = 0;
	ESP_ERROR_CHECK(gptimer_get_raw_count(alarmTimer, &timerValue));
	ESP_LOGI(alarmTag, "Counter value: %lld ticks", timerValue);
	// Inaccuracy with 3s alarm: 3 000 480

	// Must create new alarm if alarm_count changes
	gptimer_alarm_config_t alarmConfig = {
		.alarm_count = (uint64_t)seconds * B_ALARM_TIMER_RESOLUTION_HZ,
		.flags.auto_reload_on_alarm = false
	};
	ESP_ERROR_CHECK(gptimer_set_alarm_action(alarmTimer, &alarmConfig));

	// Start the timer if it hasn't been, otherwise just reset it
	if (timerValue == 0)
		ESP_ERROR_CHECK(gptimer_start(alarmTimer));
	else
		ESP_ERROR_CHECK(gptimer_set_raw_count(alarmTimer, 0));
	
	ESP_LOGI(alarmTag, "Restarted timer (%is)", seconds);
}

static void B_TimerCleanup()
{
	ESP_ERROR_CHECK(gptimer_stop(alarmTimer));
	ESP_ERROR_CHECK(gptimer_disable(alarmTimer));
	ESP_ERROR_CHECK(gptimer_del_timer(alarmTimer));

	free(alarmContainer.buffer);
}

void B_AlarmTask(void* pvParameters)
{
	const struct B_AlarmTaskParameter* const taskParameter = (const struct B_AlarmTaskParameter* const)pvParameters;
	if (taskParameter == NULL || taskParameter->addressMap == NULL) {
		ESP_LOGE(alarmTag, "The alarm task parameter is invalid, aborting startup");
		vTaskDelete(NULL);
	}

	QueueHandle_t alarmQueue = B_GetAddress(taskParameter->addressMap, B_TASKID_ALARM);
	if (!B_TimerInit(alarmQueue)) {
		ESP_LOGE(alarmTag, "Failed to create the alarm, aborting startup");
		vTaskDelete(NULL);
	}

	while (true) {
		// Block until a command is received
		B_command_t command = { 0 };
		xQueueReceive(alarmQueue, (void* const)&command, portMAX_DELAY);

		uint8_t commandOP = B_COMMAND_OP(command.header);
		uint8_t commandID = B_COMMAND_ID(command.header);

		// Message from ISR, dispatch the triggered alarm's command to it's appropriate destination
		if (commandOP == B_COMMAND_OP_RES && commandID == B_ALARM_COMMAND_TRIGGER) {
			ESP_LOGI(alarmTag, "Triggered: #%i", selectedAlarmIndex);

			if (selectedAlarmIndex == -1 || selectedAlarmIndex >= alarmContainer.size) {
				ESP_LOGW(alarmTag, "Selected alarm index is invalid at time of trigger");
				continue;
			}

			const B_command_t* triggerCommand = &alarmContainer.buffer[selectedAlarmIndex].triggerCommand;
			QueueHandle_t destQueue = B_GetAddress(taskParameter->addressMap, triggerCommand->dest);
			if (xQueueSend(destQueue, triggerCommand, 0) != pdPASS) {
				ESP_LOGE(alarmTag, "Failed to dispatch trigger command");
			}
		}

		// Insert command
		else if (commandOP == B_COMMAND_OP_SET && commandID == B_ALARM_COMMAND_INSERT) {

			B_timepart_t localTimepart = B_ReadCommandBody_DWORD(&command, 0);
			uint8_t days = B_ReadCommandBody_BYTE(&command, 4);

			ESP_LOGI(alarmTag, "Insert command: %us %u", localTimepart, days);

			// Insert command starts at body + 5
			// Body section would overflow, so only access the header bytes
			// Sanitize new alarm's trigger command (only SET, definitely no TCP or ALARM as destination)
			const B_command_t* triggerCommand = (B_command_t*)(command.body + 5);
			if (B_COMMAND_OP(triggerCommand->header) != B_COMMAND_OP_SET || triggerCommand->dest == B_TASKID_ALARM || triggerCommand->dest == B_TASKID_TCP) {
				ESP_LOGW(alarmTag, "Invalid trigger command received in new alarm");
				B_SendStatusReply(taskParameter->addressMap, B_TASKID_ALARM, command.from, B_COMMAND_OP_ERR, commandID, "Invalid trigger command");
				continue;
			}

			if (!B_InsertAlarm(&alarmContainer, localTimepart, days, triggerCommand, B_COMMAND_BODY_SIZE - 5)) {
				ESP_LOGW(alarmTag, "Error while inserting");
				B_SendStatusReply(taskParameter->addressMap, B_TASKID_ALARM, command.from, B_COMMAND_OP_ERR, commandID, "Falied to insert the alarm");
				continue;
			}

			// Reply to the sender
			B_SendStatusReply(taskParameter->addressMap, B_TASKID_ALARM, command.from, B_COMMAND_OP_RES, commandID, "Inserted alarm");
		}

		// Remove command
		else if (commandOP == B_COMMAND_OP_SET && commandID == B_ALARM_COMMAND_REMOVE) {

			uint8_t removeIndex = B_ReadCommandBody_BYTE(&command, 0);

			ESP_LOGI(alarmTag, "Remove command: #%u", removeIndex);

			if (!B_RemoveAlarm(&alarmContainer, removeIndex)) {
				ESP_LOGW(alarmTag, "Error while removing");
				B_SendStatusReply(taskParameter->addressMap, B_TASKID_ALARM, command.from, B_COMMAND_OP_ERR, commandID, "Falied to remove the alarm");
				continue;
			}

			// Reply to the sender
			B_SendStatusReply(taskParameter->addressMap, B_TASKID_ALARM, command.from, B_COMMAND_OP_RES, commandID, "Removed alarm");
		}

		// List command
		else if (commandOP == B_COMMAND_OP_GET && commandID == B_ALARM_COMMAND_LIST) {

			ESP_LOGI(alarmTag, "List command");

			B_command_t responseCommand = {
				.from = B_TASKID_ALARM,
				.dest = command.from,
				.header = B_COMMAND_OP_RES | B_ALARM_COMMAND_LIST
			};

			uint8_t maxFillableCount = alarmContainer.size;
			// One alarm's data takes up 5 bytes, check if all of them fit into the body
			if (alarmContainer.size * 5 > B_COMMAND_BODY_SIZE) {
				ESP_LOGW(alarmTag, "List response cannot carry all the alarm's data");
				maxFillableCount = B_COMMAND_BODY_SIZE / 5; // Integer division truncates
			}

			// Fill body
			// No need to zero init the body, because the alarm container had
			for (uint8_t i = 0; i < maxFillableCount; i++) {
				B_AlarmInfo_t* iAlarmInfo = &alarmContainer.buffer[i];
				B_FillCommandBody_DWORD(&responseCommand, i * 5, iAlarmInfo->localTimepart);
				B_FillCommandBody_BYTE(&responseCommand, i * 5 + 4, iAlarmInfo->days);
			}

			// Send back response
			QueueHandle_t responseQueue = B_GetAddress(taskParameter->addressMap, responseCommand.dest);
			if (xQueueSend(responseQueue, &responseCommand, 0) != pdPASS) {
				ESP_LOGE(alarmTag, "Failed to send data back to sender");
			}

			// No need to recalculate next alarm
			continue;
		}

		// Inspect command
		else if (commandOP == B_COMMAND_OP_GET && commandID == B_ALARM_COMMAND_INSPECT) {

			ESP_LOGI(alarmTag, "Inspect command");

			B_command_t responseCommand = {
				.from = B_TASKID_ALARM,
				.dest = command.from,
				.header = B_COMMAND_OP_RES | B_ALARM_COMMAND_INSPECT
			};

			uint8_t index = B_ReadCommandBody_BYTE(&command, 0);

			if (alarmContainer.buffer == NULL || index >= alarmContainer.size) {
				ESP_LOGW(alarmTag, "Invalid index");
				B_SendStatusReply(taskParameter->addressMap, B_TASKID_ALARM, command.from, B_COMMAND_OP_ERR, commandID, "Invalid index");
				continue;
			}

			// Copy trigger command into response
			// No need to zero init the body, because the alarm container had
			const B_command_t* triggerCommand = &alarmContainer.buffer[index].triggerCommand;
			memcpy(&responseCommand.body, triggerCommand, B_COMMAND_BODY_SIZE);

			// Send back response
			QueueHandle_t responseQueue = B_GetAddress(taskParameter->addressMap, responseCommand.dest);
			if (xQueueSend(responseQueue, &responseCommand, 0) != pdPASS) {
				ESP_LOGE(alarmTag, "Failed to send data back to sender");
			}

			// No need to recalculate next alarm
			continue;
		}

		// Not valid command
		else {
			B_SendStatusReply(taskParameter->addressMap, B_TASKID_ALARM, command.from, B_COMMAND_OP_ERR, commandID, "Invalid command");
			continue;
		}

		// If there is a next alarm (the container isn't empty) restart the timer, otherwise stop the timer
		B_timepart_t trigTime = B_FindNextAlarm(&selectedAlarmIndex);
		if (selectedAlarmIndex != -1){
			B_RestartTimer(trigTime);
		}
		else {
			ESP_ERROR_CHECK(gptimer_stop(alarmTimer));
			ESP_ERROR_CHECK(gptimer_set_raw_count(alarmTimer, 0));
		}
	}

	// Task paniced, clean up and delete task
	B_TimerCleanup();
	vTaskDelete(NULL);
}
