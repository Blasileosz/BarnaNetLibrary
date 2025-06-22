#pragma once

#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_system.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>

#include <sys/time.h>
#include <driver/gptimer.h>

#include "B_BarnaNetCommand.h"
#include "B_time.h"

// Cpu freq is configured in the menuconfig an is defined: CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
// APB_CLK (GPTIMER_CLK_SRC_DEFAULT) is highly dependent on the CPU_CLK source
// https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/api-reference/peripherals/clk_tree.html
// (divider >= 2 && divider <= 65536)
// For a 160 MHz processor the smallest timer resolution is 2 441 Hz
#define B_ALARM_TIMER_RESOLUTION_HZ (uint64_t)(1 * 1000 * 1000) // 1 MHz

// uint64_t max:	18 446 744 073 709 551 615	ticks
//					  ~18 446 744 073 709	seconds
//							213 503 982	days

// Must be in order, in order to be able to find next day
enum B_DAYS {
	B_MONDAY = 0b00000010, // 1.
	B_TUESDAY = 0b00000100, // 2.
	B_WEDNESDAY = 0b00001000, // 3.
	B_THURSDAY = 0b00010000, // 4.
	B_FRIDAY = 0b00100000, // 5.
	B_SATURDAY = 0b01000000, // 6.
	B_SUNDAY = 0b00000001, // 0.
	B_WEEKDAYS = 0b00111110,
	B_WEEKENDS = 0b01000001,
	B_EVERYDAY = 0b01111111
};

static_assert(B_WEEKENDS == (B_SATURDAY | B_SUNDAY), "Days incorrectly defined");
static_assert(B_WEEKDAYS == (B_MONDAY | B_TUESDAY | B_WEDNESDAY | B_THURSDAY |  B_FRIDAY), "Days incorrectly defined");
static_assert(B_EVERYDAY == (B_MONDAY | B_TUESDAY | B_WEDNESDAY | B_THURSDAY | B_FRIDAY | B_SATURDAY | B_SUNDAY), "Days incorrectly defined");

// Max timepart is 86399
#define B_ALARM_TRIGGER_SUNRISE 0xFFFFFFFF
#define B_ALARM_TRIGGER_SUNSET 0xFFFFFFFF - 1

// TODO: Will inevitably be truncated when put into a commands data field
typedef struct {
	B_timepart_t localTimepart; // Defined in local time, also stores if triggers at sunset or sunrise
	uint8_t days; // The days of the week the  alarm should trigger
	B_command_t triggerCommand;
} B_AlarmInfo_t;

struct B_AlarmContainer{
	B_AlarmInfo_t* buffer;
	int size;
};

// -- COMMANDS -- //
// DEST: B_COMMAND_DEST_ALARM
enum B_ALARM_COMMAND_IDS {
	B_ALARM_COMMAND_TRIGGER, // Used only internally, to signal to task from ISR
	B_ALARM_COMMAND_INSERT,
	B_ALARM_COMMAND_REMOVE,
	B_ALARM_COMMAND_LIST
};

// EXAMPLES:

// RESPOND TRIGGER - Expected data: none
// - Example: [B_COMMAND_OP_RES | B_COMMAND_DEST_ALARM, B_ALARM_COMMAND_TRIGGER, unused]

// SET INSERT - Expected data: B_AlarmInfo_t
// - Example: [B_COMMAND_OP_SET | B_COMMAND_DEST_ALARM, B_ALARM_COMMAND_INSERT, unused, TIMEPART0, TIMEPART1, TIMEPART2, TIMEPART3, DAYS, DATA * 125]
// RESPOND INSERT - Response
// - bytes 0: insert status
// - Example: [B_COMMAND_OP_RES | B_COMMAND_DEST_ALARM, B_ALARM_COMMAND_INSERT, unused, STATUS]

// SET REMOVE - Expected data: index
// - Example: [B_COMMAND_OP_SET | B_COMMAND_DEST_ALARM, B_ALARM_COMMAND_REMOVE, unused, INDEX0, INDEX1, INDEX2, INDEX3]
// RESPOND REMOVE - Response
// - bytes 0: remove status
// - Example: [B_COMMAND_OP_RES | B_COMMAND_DEST_ALARM, B_ALARM_COMMAND_REMOVE, unused, STATUS]


// TODO: assume only LED commands can be triggered?
// GET LIST - Expected data: none
// - Example: [B_COMMAND_OP_GET | B_COMMAND_DEST_ALARM, B_ALARM_COMMAND_LIST, unused]
// RESPOND LIST - Response
// - bytes 0: remove status
// - Example: [B_COMMAND_OP_RES | B_COMMAND_DEST_ALARM, B_ALARM_COMMAND_LIST, unused, STATUS]


// -- DEFINITIONS -- //

// static bool IRAM_ATTR B_AlarmCallback(gptimer_handle_t timer, const gptimer_alarm_event_data_t* eventData, void* userData)
// https://esp32.com/viewtopic.php?t=4978#p21478
// ISR function must comply with the gptimer_alarm_cb_t definition
// - Private function

// static bool B_InitTimer(QueueHandle_t* alarmCommandQueue)
// - Private function

// static void B_RestartTimer(int seconds)
// - Private function

// static void B_CleanupTimer()
// - Private function

bool B_InsertAlarm(struct B_AlarmContainer* const container, const B_AlarmInfo_t* const newAlarm);

bool B_RemoveAlarm(struct B_AlarmContainer* const container, int index);

// static void B_PrintNextTriggerDelta(int returnValue)
// - Private function

// static int B_AlarmFindNextTrigger(const B_AlarmInfo_t* const alarm, const struct tm* const localTime)
// - Private function

// static int B_AlarmFindNextTrigger2(const B_AlarmInfo_t* const alarm, const struct tm* const localTime)
// - Private function
// - Test

// static int B_FindNextAlarm(int* outAlarmIndex)
// - Private function

struct B_AlarmTaskParameter{
	QueueHandle_t* alarmCommandQueue;
	QueueHandle_t* tcpCommandQueue;
	void (*handlerFunctionPointer)(const B_command_t* const);
};

void B_AlarmTask(void* pvParameters);
