#include "B_alarm.h"

static const char* alarmTag = "BarnaNet - Alarm";

static struct B_AlarmContainer alarmContainer = {.buffer = NULL, .size = 0};
static int selectedAlarmIndex = -1;
static gptimer_handle_t alarmTimer;

// ISR function must comply with the gptimer_alarm_cb_t definition
static bool IRAM_ATTR B_AlarmCallback(gptimer_handle_t timer, const gptimer_alarm_event_data_t* eventData, void* userData)
{
	BaseType_t highPriorityTaskWoken = pdFALSE;
	QueueHandle_t queue = (QueueHandle_t)userData;

	B_command_t sendCommand = { 0 };

	sendCommand.header = B_COMMAND_OP_RES | B_COMMAND_DEST_ALARM;
	sendCommand.ID = B_ALARM_COMMAND_TRIGGER;

	xQueueSendFromISR(queue, &sendCommand, &highPriorityTaskWoken);

	// Cannot ESP_ERROR_CHECK in ISR

	return highPriorityTaskWoken == pdTRUE;
}

static bool B_InitTimer(QueueHandle_t* alarmCommandQueue)
{
	// Create timer
	gptimer_config_t timerConfig = {
		.clk_src = GPTIMER_CLK_SRC_DEFAULT,
		.direction = GPTIMER_COUNT_UP,
		.resolution_hz = B_ALARM_TIMER_RESOLUTION_HZ
	};
	ESP_ERROR_CHECK(gptimer_new_timer(&timerConfig, &alarmTimer));

	// SOC_TIMER_GROUP_COUNTER_BIT_WIDTH shows the width of the counter

	// Create alarm
	gptimer_alarm_config_t alarmConfig = {
		.alarm_count = 5 * B_ALARM_TIMER_RESOLUTION_HZ, // 5s
		.flags.auto_reload_on_alarm = false
	};
	ESP_ERROR_CHECK(gptimer_set_alarm_action(alarmTimer, &alarmConfig));

	// Register callback
	gptimer_event_callbacks_t callbackConfig = {
		.on_alarm = B_AlarmCallback
	};
	// Pass queue handle by value
	ESP_ERROR_CHECK(gptimer_register_event_callbacks(alarmTimer, &callbackConfig, *alarmCommandQueue));

	// Enable timer
	ESP_ERROR_CHECK(gptimer_enable(alarmTimer));

	// Set 5s value to the timer
	ESP_ERROR_CHECK(gptimer_set_raw_count(alarmTimer, 5 * B_ALARM_TIMER_RESOLUTION_HZ));

	// Start timer
	ESP_ERROR_CHECK(gptimer_start(alarmTimer));


	// Allocate alarm container
	alarmContainer.buffer = calloc(CONFIG_B_ALARM_CONTAINER_CAPACITY, sizeof(B_AlarmInfo_t));
	if (alarmContainer.buffer == NULL)
		return false;

	return true;
}

static void B_RestartTimer(int seconds)
{
	// Reset timer value (no need to stop for the duration of setting the new alarm count)
	uint64_t timerValue = 0;
	ESP_ERROR_CHECK(gptimer_get_raw_count(alarmTimer, &timerValue));
	ESP_LOGI(alarmTag, "Counter value: %lld ticks", timerValue);
	// Error with 3s alarm: 3 000 480

	ESP_ERROR_CHECK(gptimer_set_raw_count(alarmTimer, 0));

	// Must create new alarm if alarm_count changes
	gptimer_alarm_config_t alarmConfig = {
		.alarm_count = (uint64_t)seconds * B_ALARM_TIMER_RESOLUTION_HZ,
		.flags.auto_reload_on_alarm = false
	};
	ESP_ERROR_CHECK(gptimer_set_alarm_action(alarmTimer, &alarmConfig));
	

	ESP_LOGI(alarmTag, "Restarted timer (%is)", seconds);
}

static void B_CleanupTimer()
{
	ESP_ERROR_CHECK(gptimer_stop(alarmTimer));
	ESP_ERROR_CHECK(gptimer_disable(alarmTimer));
	ESP_ERROR_CHECK(gptimer_del_timer(alarmTimer));

	free(alarmContainer.buffer);
}

bool B_InsertAlarm(struct B_AlarmContainer* const container, const B_AlarmInfo_t* const newAlarm)
{
	if (container->buffer == NULL || container->size == CONFIG_B_ALARM_CONTAINER_CAPACITY) {
		ESP_LOGI(alarmTag, "Failed to insert alarm into container");
		return false;
	}

	container->buffer[container->size].localTimepart = newAlarm->localTimepart;
	container->buffer[container->size].days = newAlarm->days;
	memcpy(&container->buffer[container->size].triggerCommand, &newAlarm->triggerCommand, sizeof(B_command_t));
	container->size++;
	return true;
}

bool B_RemoveAlarm(struct B_AlarmContainer* const container, int index)
{
	if (container->buffer == NULL || container->size <= index) {
		ESP_LOGI(alarmTag, "Failed to remove alarm from container");
		return false;
	}

	// Copy from back of the buffer to the index
	container->buffer[index].localTimepart = container->buffer[container->size].localTimepart;
	container->buffer[index].days = container->buffer[container->size].days;
	memcpy(&container->buffer[index].triggerCommand, &container->buffer[container->size].triggerCommand, sizeof(B_command_t));
	container->size--;
	return true;
}

static void B_PrintNextTriggerDelta(int returnValue)
{
	ESP_LOGI(alarmTag, "%i days, %02i:%02i:%02i", (returnValue / (24 * 3600)), B_TimepartGetHours(returnValue), B_TimepartGetMinutes(returnValue), B_TimepartGetSeconds(returnValue));
}

static int B_AlarmFindNextTrigger(const B_AlarmInfo_t* const alarm, const struct tm* const localTime)
{
	int wday = localTime->tm_wday;
	int timePartNow = B_BuildTimepart(localTime->tm_hour, localTime->tm_min, localTime->tm_sec);
	B_timepart_t alarmTrigTime = alarm->localTimepart;

	if (alarmTrigTime == B_ALARM_TRIGGER_SUNRISE)
		alarmTrigTime = B_GetLocalTimepart(B_SUNRISE_TABLE[localTime->tm_mon][localTime->tm_mday]);
	else if (alarmTrigTime == B_ALARM_TRIGGER_SUNSET)
		alarmTrigTime = B_GetLocalTimepart(B_SUNSET_TABLE[localTime->tm_mon][localTime->tm_mday]);

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

static int B_AlarmFindNextTrigger2(const B_AlarmInfo_t* const alarm, const struct tm* const localTime)
{
	int wday = localTime->tm_wday;
	int timePartNow = B_BuildTimepart(localTime->tm_hour, localTime->tm_min, localTime->tm_sec);
	B_timepart_t alarmTrigTime = alarm->localTimepart;

	if (alarmTrigTime == B_ALARM_TRIGGER_SUNRISE)
		alarmTrigTime = B_GetLocalTimepart(B_SUNRISE_TABLE[localTime->tm_mon][localTime->tm_mday]);
	else if (alarmTrigTime == B_ALARM_TRIGGER_SUNSET)
		alarmTrigTime = B_GetLocalTimepart(B_SUNRISE_TABLE[localTime->tm_mon][localTime->tm_mday]);

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

static int B_FindNextAlarm(int* outAlarmIndex)
{
	time_t now = 0; // UTC time
	time(&now);
	struct tm timeStruct = { 0 }; // Local time
	localtime_r(&now, &timeStruct);

	int minTrigTime = 0x7FFFFFFF;
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

	if (outAlarmIndex != NULL)
		*outAlarmIndex = closestAlarmIndex;

	return minTrigTime;
}

void B_AlarmTask(void* pvParameters)
{
	const struct B_AlarmTaskParameter* const alarmTaskParameter = (const struct B_AlarmTaskParameter* const)pvParameters;
	if (alarmTaskParameter == NULL || alarmTaskParameter->alarmCommandQueue == NULL || alarmTaskParameter->handlerFunctionPointer == NULL || alarmTaskParameter->tcpCommandQueue == NULL) {
		ESP_LOGE(alarmTag, "The alarm task parameter is invalid, aborting startup");
		vTaskDelete(NULL);
	}

	if (!B_InitTimer(alarmTaskParameter->alarmCommandQueue)) {
		ESP_LOGE(alarmTag, "Failed to create the alarm, aborting startup");
		vTaskDelete(NULL);
	}

	// Test ---
	B_AlarmInfo_t testAlarm = {.days = B_EVERYDAY, .localTimepart = B_BuildTimepart(12, 10, 00)};
	B_InsertAlarm(&alarmContainer, &testAlarm);

	testAlarm.localTimepart = B_BuildTimepart(12, 12, 0);
	B_InsertAlarm(&alarmContainer, &testAlarm);

	testAlarm.localTimepart = B_ALARM_TRIGGER_SUNSET;
	B_InsertAlarm(&alarmContainer, &testAlarm);
	// ---


	ESP_LOGI(alarmTag, "Alarm timer created, configured and started");

	while (true) {
		// Block until a command is received
		B_command_t receiveCommand = { 0 };
		xQueueReceive(*alarmTaskParameter->alarmCommandQueue, &receiveCommand, portMAX_DELAY);

		ESP_LOGI(alarmTag, "Alarm event triggered");

		// switch (receiveCommand.ID)
		// {
		// case B_ALARM_COMMAND_TRIGGER:
		// 	{
				
		// 		break;
		// 	}
		// }

		// Message from ISR, dispatch to handler function
		if (receiveCommand.header == B_COMMAND_OP_RES | B_COMMAND_DEST_ALARM && receiveCommand.ID == B_ALARM_COMMAND_TRIGGER) {

			if (selectedAlarmIndex == -1 || selectedAlarmIndex >= alarmContainer.size) {
				ESP_LOGE(alarmTag, "Selected alarm index is invalid at time of trigger");
				continue;
			}

			// Copy the triggered alarm's command to the receive buffer and send it onwards
			memcpy(&receiveCommand, &alarmContainer.buffer[selectedAlarmIndex].triggerCommand, sizeof(B_command_t));

			// Only the state changes need outside handling
			alarmTaskParameter->handlerFunctionPointer(&receiveCommand);
		}

		// Insert command
		if (receiveCommand.header == B_COMMAND_OP_SET | B_COMMAND_DEST_ALARM && receiveCommand.ID == B_ALARM_COMMAND_INSERT) {

			// if (alarmContainer.size == CONFIG_B_ALARM_CONTAINER_CAPACITY) {
			// 	ESP_LOGE(alarmTag, "Alarm container is at capacity")
			// }

			B_AlarmInfo_t receivedAlarm = {0};
			receivedAlarm.localTimepart = *((B_timepart_t*)&receiveCommand.data[0]);
			receivedAlarm.days = receiveCommand.data[4];
			memcpy(&receivedAlarm.triggerCommand, &receiveCommand.data[5], sizeof(receiveCommand.data)); // Only 125 bytes are copied, see B_AlarmInfo_t definition for the problem

			if (!B_InsertAlarm(&alarmContainer, &receivedAlarm)){
				ESP_LOGE(alarmTag, "Error while inserting");
			}

			// TODO: reply
		}

		// Remove command
		if (receiveCommand.header == B_COMMAND_OP_SET | B_COMMAND_DEST_ALARM && receiveCommand.ID == B_ALARM_COMMAND_REMOVE) {

			int removeIndex = -1;
			removeIndex = *((int*)&receiveCommand.data[0]); // Handle negative data

			if (!B_RemoveAlarm(&alarmContainer, removeIndex)) {
				ESP_LOGE(alarmTag, "Error while removing");
			}

			// TODO: reply
		}


		// TODO: handle case when alarmContainer is empty
		int trigTime = B_FindNextAlarm(&selectedAlarmIndex);
		B_RestartTimer(trigTime);

	}

	// Task paniced, clean up and delete task
	B_CleanupTimer();
	vTaskDelete(NULL);
}
