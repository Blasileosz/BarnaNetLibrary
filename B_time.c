#include "B_time.h"

static const char* timeTag = "BarnaNet - TIME";

B_timepart_t B_GetTimepart(time_t timestamp)
{
	return timestamp % (24 * 3600);
}

B_timepart_t B_BuildTimepart(int hours, int minutes, int seconds)
{
	return hours * 3600 + minutes * 60 + seconds;
}

int B_GetUTCOffset()
{
	time_t now = time(NULL);
	struct tm localTM = { 0 };
	localtime_r(&now, &localTM);

	B_timepart_t localTimepart = B_BuildTimepart(localTM.tm_hour, localTM.tm_min, localTM.tm_sec);
	B_timepart_t utcTimepart = B_GetTimepart(now);

	return localTimepart - utcTimepart;
}

B_timepart_t B_GetLocalTimepart(B_timepart_t timepart)
{
	int utcOffset = B_GetUTCOffset();
	return timepart + utcOffset;
}

int B_TimepartGetSeconds(B_timepart_t timePart)
{
	return timePart % 60;
}

int B_TimepartGetMinutes(B_timepart_t timePart)
{
	return (timePart / 60) % 60;
}

int B_TimepartGetHours(B_timepart_t timePart)
{
	return (timePart / 3600) % 24;
}

bool B_SyncTime()
{
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_LOGI(timeTag, "Initializing SNTP");

	esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(B_SNTP_SERVER);
	config.start = true;		// Automatically starts
	esp_netif_sntp_init(&config); // Starts time sync

	// Wait for time sync event
	int retry = 0;
	while (esp_netif_sntp_sync_wait(B_SNTP_TIMEOUT_MS / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < B_SNTP_MAX_RETRY) {
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

void B_SntpCleanup()
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

bool B_IsLeapYear(int year)
{
	return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

time_t B_JulianToTimestamp(double julian)
{
	// 2440587.5 is 1970 January 1, to get the days since epoch
	// 86400 is seconds a day, to get the seconds since the epoch
	return (time_t)((julian - 2440587.5f) * 86400);
}

double B_TimestampToJulian(time_t timestamp)
{
	return (float)timestamp / 86400.0f + 2440587.5f;
}

B_timepart_t B_JulianToTimepart(double julian)
{
	double fractionalPart = julian - floor(julian); // Decimal part represents the time of day
	return (B_timepart_t)(fractionalPart * 86400);
}

double B_Radians(double degrees)
{
	return degrees * (M_PI / 180);
}

double B_Degrees(double radians)
{
	return radians * (180.0 / M_PI);
}

void B_CalculateSunSetRise(time_t timestamp, B_timepart_t* sunriseOut, B_timepart_t* sunsetOut)
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

	*sunriseOut = B_JulianToTimepart(J_transit - HA_degrees / 360);
	*sunsetOut = B_JulianToTimepart(J_transit + HA_degrees / 360);
}

const B_timepart_t B_SUNRISE_TABLE[12][31] = {
	{23348,	23348,	23345,	23338,	23329,	23317,	23303,	23285,	23265,	23241,	23216,	23187,	23156,	23122,	23085,	23046,	23004,	22960,	22913,	22864,	22813,	22759,	22702,	22644,	22583,	22520,	22454,	22387,	22317,	22246,	22172},
	{22097,	22019,	21940,	21859,	21776,	21691,	21604,	21516,	21426,	21335,	21242,	21147,	21051,	20954,	20855,	20755,	20654,	20551,	20447,	20342,	20236,	20128,	20020,	19910,	19800,	19688,	19576,	19462,	19348,	0,	0},
	{19233,	19118,	19001,	18884,	18766,	18647,	18528,	18409,	18288,	18168,	18047,	17925,	17803,	17681,	17558,	17436,	17312,	17189,	17066,	16942,	16819,	16695,	16571,	16447,	16324,	16200,	16077,	15953,	15830,	15707,	15584},
	{15462,	15340,	15218,	15097,	14976,	14855,	14735,	14616,	14497,	14378,	14261,	14144,	14027,	13912,	13797,	13683,	13569,	13457,	13345,	13235,	13125,	13017,	12909,	12803,	12697,	12593,	12490,	12388,	12287,	12188,	0},
	{12090,	11993,	11897,	11803,	11711,	11620,	11531,	11443,	11356,	11272,	11189,	11108,	11028,	10950,	10874,	10800,	10728,	10658,	10590,	10524,	10460,	10398,	10338,	10280,	10224,	10171,	10120,	10071,	10024,	9980,	9938},
	{9899,	9862,	9827,	9795,	9766,	9739,	9714,	9692,	9673,	9656,	9641,	9630,	9620,	9614,	9610,	9608,	9610,	9613,	9620,	9628,	9640,	9653,	9670,	9688,	9710,	9733,	9759,	9787,	9818,	9851,	0},
	{9885,	9923,	9962,	10003,	10047,	10092,	10139,	10188,	10239,	10292,	10347,	10403,	10460,	10520,	10580,	10643,	10706,	10771,	10837,	10904,	10973,	11042,	11113,	11184,	11257,	11330,	11404,	11479,	11555,	11631,	11708},
	{11786,	11864,	11942,	12021,	12100,	12180,	12260,	12341,	12421,	12502,	12583,	12665,	12746,	12828,	12909,	12991,	13073,	13155,	13236,	13318,	13400,	13482,	13564,	13645,	13727,	13809,	13890,	13972,	14053,	14135,	14216},
	{14297,	14378,	14459,	14540,	14621,	14702,	14783,	14864,	14945,	15025,	15106,	15187,	15268,	15348,	15429,	15510,	15591,	15672,	15753,	15834,	15915,	15997,	16078,	16160,	16242,	16324,	16406,	16489,	16571,	16654,	0},
	{16737,	16821,	16904,	16988,	17073,	17157,	17242,	17328,	17413,	17499,	17585,	17672,	17759,	17846,	17934,	18022,	18111,	18199,	18289,	18378,	18468,	18558,	18649,	18739,	18831,	18922,	19014,	19106,	19198,	19290,	19383},
	{19475,	19568,	19661,	19754,	19847,	19940,	20033,	20125,	20218,	20311,	20403,	20495,	20586,	20678,	20768,	20859,	20948,	21038,	21126,	21214,	21301,	21387,	21472,	21556,	21639,	21721,	21801,	21881,	21958,	22035,	0},
	{22110,	22183,	22255,	22325,	22393,	22460,	22524,	22587,	22647,	22705,	22761,	22815,	22867,	22916,	22963,	23007,	23049,	23088,	23124,	23158,	23189,	23218,	23244,	23267,	23287,	23304,	23319,	23330,	23339,	23345,	23348}
};

const B_timepart_t B_SUNSET_TABLE[12][31] = {
	{53791,	53847,	53905,	53965,	54028,	54092,	54159,	54228,	54298,	54371,	54445,	54521,	54598,	54677,	54758,	54840,	54923,	55008,	55094,	55181,	55269,	55358,	55447,	55538,	55630,	55722,	55815,	55908,	56002,	56097,	56191},
	{56287,	56382,	56478,	56574,	56670,	56767,	56863,	56959,	57056,	57152,	57249,	57345,	57441,	57537,	57633,	57728,	57824,	57919,	58014,	58109,	58203,	58297,	58391,	58484,	58577,	58670,	58762,	58854,	58946,	0,	0},
	{59038,	59129,	59219,	59310,	59400,	59490,	59579,	59668,	59757,	59846,	59934,	60022,	60110,	60198,	60285,	60372,	60459,	60546,	60632,	60719,	60805,	60891,	60977,	61063,	61149,	61235,	61320,	61406,	61491,	61577,	61662},
	{61748,	61833,	61919,	62004,	62090,	62175,	62261,	62346,	62432,	62518,	62604,	62689,	62775,	62861,	62947,	63033,	63119,	63205,	63291,	63377,	63464,	63550,	63636,	63722,	63808,	63893,	63979,	64065,	64150,	64235,	0},
	{64321,	64405,	64490,	64574,	64658,	64742,	64825,	64908,	64990,	65072,	65153,	65234,	65313,	65393,	65471,	65549,	65626,	65701,	65776,	65850,	65923,	65995,	66065,	66135,	66203,	66269,	66335,	66398,	66461,	66521,	66580},
	{66638,	66693,	66747,	66799,	66849,	66897,	66942,	66986,	67028,	67068,	67105,	67140,	67173,	67203,	67231,	67257,	67280,	67301,	67319,	67335,	67348,	67358,	67366,	67371,	67374,	67374,	67372,	67367,	67359,	67348,	0},
	{67335,	67320,	67301,	67281,	67257,	67231,	67202,	67171,	67138,	67101,	67063,	67022,	66978,	66932,	66884,	66834,	66781,	66726,	66668,	66609,	66547,	66483,	66417,	66349,	66280,	66208,	66134,	66058,	65980,	65901,	65820},
	{65737,	65652,	65565,	65477,	65388,	65297,	65204,	65110,	65014,	64917,	64819,	64719,	64618,	64516,	64412,	64307,	64201,	64094,	63986,	63877,	63767,	63655,	63543,	63430,	63316,	63201,	63086,	62969,	62852,	62734,	62615},
	{62496,	62376,	62256,	62135,	62013,	61891,	61769,	61646,	61522,	61399,	61275,	61150,	61026,	60901,	60776,	60651,	60526,	60400,	60275,	60150,	60024,	59899,	59774,	59648,	59523,	59398,	59274,	59149,	59025,	58902,	0},
	{58778,	58655,	58532,	58410,	58289,	58167,	58047,	57927,	57807,	57689,	57571,	57453,	57337,	57221,	57106,	56992,	56879,	56767,	56656,	56546,	56437,	56329,	56223,	56117,	56013,	55910,	55808,	55708,	55609,	55511,	55415},
	{55320,	55227,	55135,	55045,	54957,	54871,	54786,	54703,	54621,	54542,	54465,	54389,	54316,	54244,	54175,	54107,	54042,	53979,	53919,	53860,	53804,	53751,	53699,	53651,	53604,	53561,	53519,	53481,	53445,	53411,	0},
	{53381,	53353,	53328,	53305,	53286,	53269,	53255,	53244,	53236,	53230,	53228,	53228,	53232,	53238,	53247,	53260,	53275,	53293,	53313,	53337,	53364,	53393,	53425,	53460,	53497,	53537,	53580,	53626,	53673,	53724,	53777}
};
