# Source: https://en.wikipedia.org/wiki/Sunrise_equation#Example_of_implementation_in_Python

from datetime import datetime, timezone, tzinfo
from math import acos, asin, ceil, cos, degrees, fmod, radians, sin, sqrt, trunc
from time import time

def ts2human(ts: int | float, tzinfo: tzinfo | None) -> str:
	return str(datetime.fromtimestamp(ts, tzinfo))


def j2ts(j: float | int) -> float:
	return (j - 2440587.5) * 86400


def ts2j(ts: float | int) -> float:
	return ts / 86400.0 + 2440587.5


def j2human(j: float | int, tzinfo: tzinfo | None) -> str:
	ts = j2ts(j)
	return f'{ts} = {ts2human(ts, tzinfo)}'


def deg2human(deg: float | int) -> str:
	x = int(deg * 3600.0)
	num = f'∠{deg:.3f}°'
	rad = f'∠{radians(deg):.3f}rad'
	human = f'∠{x // 3600}°{x // 60 % 60}′{x % 60}″'
	return f'{rad} = {human} = {num}'

def get_timepart(timestamp):
	return timestamp % (24 * 3600)

def calc(
	current_timestamp: float,
	f: float,
	l_w: float,
	elevation: float = 0.0
) -> tuple[float, float, None] | tuple[None, None, bool]:

	J_date = ts2j(current_timestamp)

	# Julian day
	n = ceil(J_date - (2451545.0 + 0.0009) + 69.184 / 86400.0)

	# Mean solar time
	J_ = n + 0.0009 - l_w / 360.0

	# Solar mean anomaly
	# M_degrees = 357.5291 + 0.98560028 * J_  # Same, but looks ugly
	M_degrees = fmod(357.5291 + 0.98560028 * J_, 360)
	M_radians = radians(M_degrees)

	# Equation of the center
	C_degrees = 1.9148 * sin(M_radians) + 0.02 * sin(2 * M_radians) + 0.0003 * sin(3 * M_radians)

	# Ecliptic longitude
	# L_degrees = M_degrees + C_degrees + 180.0 + 102.9372  # Same, but looks ugly
	L_degrees = fmod(M_degrees + C_degrees + 180.0 + 102.9372, 360)

	Lambda_radians = radians(L_degrees)

	# Solar transit (julian date)
	J_transit = 2451545.0 + J_ + 0.0053 * sin(M_radians) - 0.0069 * sin(2 * Lambda_radians)

	# Declination of the Sun
	sin_d = sin(Lambda_radians) * sin(radians(23.4397))
	cos_d = cos(asin(sin_d))

	# Hour angle
	some_cos = (sin(radians(-0.833 - 2.076 * sqrt(elevation) / 60.0)) - sin(radians(f)) * sin_d) / (cos(radians(f)) * cos_d)
	try:
		w0_radians = acos(some_cos)
	except ValueError:
		return None, None, some_cos > 0.0
	w0_degrees = degrees(w0_radians)  # 0...180

	j_rise = J_transit - w0_degrees / 360
	j_set = J_transit + w0_degrees / 360

	return j2ts(j_rise), j2ts(j_set), None

# Calculate leap year (2024) to make sure there is enough data
months = [31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
bakedSunrise = [[0] * 31 for _ in range(12)] #It's a trap: [[0] * 31] * 12
bakedSunset = [[0] * 31 for _ in range(12)]

def bake_year(latitude, longitude, elevation) -> None:
	for month, monthLen in enumerate(months, start=1):
		for day in range(1, monthLen + 1):
			dt = datetime(year=2024, month=month, day=day, tzinfo=timezone.utc)
			timestamp = dt.timestamp()
			(sunrise, sunset, error) = calc(timestamp, latitude, longitude, elevation)
			bakedSunrise[month - 1][day - 1] = trunc(get_timepart(sunrise))
			bakedSunset[month - 1][day - 1] = trunc(get_timepart(sunset))

def format_bake():
	outputFile = open("./bakedSun.txt", "wt")
	for month in bakedSunrise:
		outputFile.write('{' + ',\t'.join(map(str, month)) + '},\n')

	outputFile.write("\n")

	for month in bakedSunset:
		outputFile.write('{' + ',\t'.join(map(str, month)) + '},\n')
	
	outputFile.close()

def main():
	latitude = 47.896076
	longitude = 20.380324
	elevation = 0
	sample_time = time()
	#(sunrise, sunset, error) = calc(sample_time, latitude, longitude, elevation)
	#print(ts2human(sunrise, timezone.utc))
	#print(ts2human(sunset, timezone.utc))
	bake_year(latitude, longitude, elevation)
	format_bake()

if __name__ == '__main__':
	main()
