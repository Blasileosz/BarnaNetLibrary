#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_system.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_netif_sntp.h>
#include <nvs_flash.h>

#include <sys/time.h>
#include <driver/gptimer.h>

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>

#define _USE_MATH_DEFINES
#include <math.h>

// Set timezone https://developer.ibm.com/articles/au-aix-posix/
// Timezone:	CET (DST off)
//			CEST (DST on)
// UTC Offset:	+1
// CEST	start:	Last Sunday of March  at 02:00 AM
//		end:		Last Sunday of October at 03:00 AM
// (assuming 5 sundays in a month)

#define B_TIMEZONE "CET-1CEST,M3.5.0/2:00:00,M10.5.0/3:00:00"
#define B_LATITUDE (47.896076) // 0.8359r - 47.896076°
#define B_LONGITUDE (20.380324) // 0.3557r - 20.380324°
#define B_ELEVATION 0

#define B_SNTP_SERVER "hu.pool.ntp.org"
#define B_SNTP_MAX_RETRY 15
#define B_SNTP_TIMEOUT_MS 2000

// Timepart
// Holds the hours, minutes and seconds
typedef uint32_t B_timepart_t;

B_timepart_t B_GetTimepart(time_t timestamp); // Get timepart from time_t
B_timepart_t B_GetLocalTimepart(time_t timestamp); // Get timepart in local time from time_t
B_timepart_t B_BuildTimepart(int hours, int minutes, int seconds); // Assemble timepart from h,m,s (argument bounds are not checked)

int B_TimepartGetSeconds(B_timepart_t timepart);
int B_TimepartGetMinutes(B_timepart_t timepart);
int B_TimepartGetHours(B_timepart_t timepart);

// NTP
bool B_SyncTime();
void B_DeinitSntp();

// Debug
void B_PrintLocalTime();

// MISC
bool B_IsLeapYear(int year);
time_t B_JulianToTimestamp(double julian);
double B_TimestampToJulian(time_t timestamp);
B_timepart_t B_JulianToTimepart(double julian);

double B_Radians(double degrees);
double B_Degrees(double radians);

// Get sunset and sunrise for given timestamp (the output are in UTC)
// Reference: https://gml.noaa.gov/grad/solcalc/table.php?lat=47.896095&lon=20.380313&year=2024
// https://en.wikipedia.org/wiki/Sunrise_equation#Complete_calculation_on_Earth
void B_CalculateSunSetRise(time_t timestamp, B_timepart_t* sunriseOut, B_timepart_t* sunsetOut);

// Precalculated values for 47.896095, 20.380313, using the /utils/bakeSun.py
// Source: https://gml.noaa.gov/grad/solcalc/table.php?lat=47.896095&lon=20.380313&year=2024
// The ESP-IDF puts the const declared array into .rodata, no need to manualy specify
// Interpreted as UTC timepart
extern const B_timepart_t B_SUNRISE_TABLE[12][31];
extern const B_timepart_t B_SUNSET_TABLE[12][31];
