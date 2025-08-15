#pragma once

#define B_TASKID_ALARM 2

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
#include "B_tcpServer.h"
#include "B_time.h"

#define B_ALARM_NVS_NAMESPACE "B_ALARM"
#define B_ALARM_NVS_CONTAINER_SIZE "B_ALARM_SIZE"
#define B_ALARM_NVS_BUFFER "B_ALARM_BUFFER"

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
#define B_ALARM_TRIGGER_SUNSET (0xFFFFFFFF - 1)

// TODO: Will inevitably be truncated when put into a commands data field
typedef struct {
	B_timepart_t localTimepart; // Defined in local time, also stores if triggers at sunset or sunrise
	uint8_t days; // The days of the week the  alarm should trigger
	B_command_t triggerCommand;
} B_AlarmInfo_t;

// Buffer capacity is defined in the menuconfig
struct B_AlarmContainer {
	uint8_t size;
	B_AlarmInfo_t* buffer;
};

// How many bytes the buffer takes up
#define B_ALARM_CONTAINER_BUFFER_SIZE (CONFIG_B_ALARM_CONTAINER_CAPACITY * sizeof(B_AlarmInfo_t))

enum B_ALARM_COMMAND_IDS {
	B_ALARM_COMMAND_TRIGGER, // Used only internally, to signal to task from ISR
	B_ALARM_COMMAND_INSERT,
	B_ALARM_COMMAND_REMOVE,
	B_ALARM_COMMAND_LIST,
	B_ALARM_COMMAND_INSPECT
};


// https://esp32.com/viewtopic.php?t=4978#p21478
// ISR function must comply with the gptimer_alarm_cb_t definition
// static bool IRAM_ATTR B_AlarmInterrupt(gptimer_handle_t timer, const gptimer_alarm_event_data_t* eventData, void* userData)
// - Private function

// static void B_PrintNextTriggerDelta(int returnValue)
// - Private function

// static B_timepart_t B_AlarmFindNextTrigger(const B_AlarmInfo_t* const alarm, const struct tm* const localTime)
// - Private function

// static B_timepart_t B_AlarmFindNextTrigger2(const B_AlarmInfo_t* const alarm, const struct tm* const localTime)
// - Private function
// - Test

// static B_timepart_t B_FindNextAlarm(int* outAlarmIndex)
// - Private function

// static void B_LoadAlarmsFromNVS()
// Must be called after the AlarmContainer buffer has been initialized!
// - Private function

// static void B_SaveAlarmsToNVS()
// - Private function

// static bool B_InsertAlarm(struct B_AlarmContainer* const container, B_timepart_t localTimepart, uint8_t days, const B_command_t* const triggerCommand, size_t commandSize);
// Does not do any sanitization or bounds checking, these should be done by the caller
// - Private function

// static bool B_RemoveAlarm(struct B_AlarmContainer* const container, uint8_t index);
// - Private function

// static bool B_TimerInit(QueueHandle_t alarmCommandQueue)
// - Private function

// static void B_RestartTimer(B_timepart_t seconds)
// - Private function

// static void B_TimerCleanup()
// - Private function

struct B_AlarmTaskParameter {
	B_addressMap_t* addressMap;
};

void B_AlarmTask(void* pvParameters);
