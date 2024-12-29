#include "B_time.h"

static const char* timeTag = "BarnaNet - TIME";

bool B_SyncTime()
{
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_LOGI(timeTag, "Initializing SNTP");

	esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(B_SNTP_SERVER);
	config.start = true;		// Automatically starts
	esp_netif_sntp_init(&config); // Starts time sync

	// Wait for time sync event
	int retry = 0;
	while (esp_netif_sntp_sync_wait(B_SNTP_TIMEOUT_MS / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < B_SNTP_MAX_RETRY)
	{
		ESP_LOGI(timeTag, "Waiting for system time to be set... (%i/%i)", retry, B_SNTP_MAX_RETRY);
	}

	// Error if failed
	if (retry == B_SNTP_MAX_RETRY)
		return false;

	// Set system timezone
	setenv("TZ", B_TIMEZONE, true);
	tzset();
	return true;
}

void B_DeinitSntp()
{
	esp_netif_sntp_deinit();
}

void B_PrintLocalTime()
{
	time_t now = 0;
	struct tm timeinfo = { 0 };
	char timeBuffer[64];
	time(&now);
	localtime_r(&now, &timeinfo);
	strftime(timeBuffer, sizeof(timeBuffer), "%Y. %m. %d. - %X", &timeinfo);
	ESP_LOGI(timeTag, "Local time: %s", timeBuffer);
}

// void SetTimer()
// {
// 	timer_group_t* tg;
// 	timer_idx_t timer_index;
// 	tg = timer_group_get_new(TIMER_GROUP_0);
// 	timer_init(tg, TIMER_0, &timer_group0_isr, NULL);
// 	timer_set_alarm_value(tg, TIMER_0, 3600000 * 12); // 12 hours timer_enable_alarm_generate(tg, TIMER_0); }
// }

// void IRAM_ATTR timer_group0_isr(void *param)
// {
// 	// Your interrupt handling code here
// }

bool B_IsLeapYear(int year)
{
	return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

time_t B_JulianToTimestamp(double julian)
{
	return (time_t)((julian - 2440587.5f) * 86400);
}

double B_TimestampToJulian(time_t timestamp)
{
	return (float)timestamp / 86400.0f + 2440587.5f;
}

double B_Radians(double degrees)
{
	return degrees * (M_PI / 180);
}

double B_Degrees(double radians)
{
	return radians * (180.0 / M_PI);
}

void B_CalculateSunSetRise(time_t timestamp, time_t* sunriseOut, time_t* sunsetOut)
{
	// Julian calendar starts at 4713 BC and counts the days since
	double julianDate = B_TimestampToJulian(timestamp);

	// 2451545.0 is the equivalent Julian year of Julian days for Jan-01-2000, 12:00:00
	// 0.0008 is the fractional Julian Day for leap seconds and terrestrial time (TT)
	double julianDay = ceil(julianDate - (2451545.0 + 0.0009) + 69.184 / 86400.0);

	// The approximation of mean solar time at the Julian day with the day fraction
	double meanSolarTime = julianDay + 0.0009 - B_LONGITUDE / 360.0;

	// Solar mean anomaly
	double M_degrees = fmod(357.5291 + 0.98560028 * meanSolarTime, 360);
	double M_radians = B_Radians(M_degrees);

	// Equation of the center
	double C_degrees = 1.9148 * sin(M_radians) + 0.02 * sin(2 * M_radians) + 0.0003 * sin(3 * M_radians);

	// Ecliptic longitude
	double L_degrees = fmod(M_degrees + C_degrees + 180.0 + 102.9372, 360);
	double L_radians = B_Radians(L_degrees);

	// Solar transit - The Jlian date for the solar noon
	double J_transit = 2451545.0 + meanSolarTime + 0.0053 * sin(M_radians) - 0.0069 * sin(2 * L_radians);
	
	// Declination of the Sun
	double sin_d = sin(L_radians) * sin(B_Radians(23.4397));
	double cos_d = cos(asin(sin_d));

	// Hour angle
	double HA_cos = (sin(B_Radians(-0.833 - 2.076 * sqrt(B_ELEVATION) / 60.0)) - sin(B_Radians(B_LATITUDE)) * sin_d) / (cos(B_Radians(B_LATITUDE)) * cos_d);
	double HA_radians = acos(HA_cos);
	double HA_degrees = B_Degrees(HA_radians);

	*sunriseOut = B_JulianToTimestamp(J_transit - HA_degrees / 360);
	*sunsetOut = B_JulianToTimestamp(J_transit + HA_degrees / 360);
}
