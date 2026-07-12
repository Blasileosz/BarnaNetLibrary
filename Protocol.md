# BarnaNetLibrary API endpoints

## Alarm task
- Task id = 2
- Days definition:
	- `B_MONDAY = 0b00000010`
	- `B_TUESDAY = 0b00000100`
	- `B_WEDNESDAY = 0b00001000`
	- `B_THURSDAY = 0b00010000`
	- `B_FRIDAY = 0b00100000`
	- `B_SATURDAY = 0b01000000`
	- `B_SUNDAY = 0b00000001`
	- `B_WEEKDAYS = 0b00111110`
	- `B_WEEKENDS = 0b01000001`
	- `B_EVERYDAY = 0b01111111`
- To signal sunrise or sunset trigger, the timepart has two special values:
	- `B_ALARM_TRIGGER_SUNRISE = 0xFFFFFFFF`
	- `B_ALARM_TRIGGER_SUNSET = 0xFFFFFFFF - 1`


### INSERT
- ID: 1
- OP: SET
	- Request layout: `[TIMEPART0, TIMEPART1, TIMEPART2, TIMEPART3, DAYS, COMMAND * (B_COMMAND_BODY_SIZE - 5)]`
		- TIMEPART: An unsigned 32bit value that stores the trigger time in seconds
		- DAYS: An 8bit binary set that indicates which days the alarm should trigger
		- COMMAND: A buffer to store the command to be executed when the alarm triggers
			- Cannot store a whole B_COMMAND_STRUCT_SIZE long command due to being restrained by itself
	- Response layout: A plaintext status

### REMOVE
- ID: 2
- OP: SET
	- Request layout: `[INDEX]`
		- INDEX: The index of the alarm to be removed
	- Response layout: A plaintext status

### LIST
- ID: 3
- OP: GET
	- Request layout: No data required
	- Response layout: `[ALARM_COUNT, TIMEPART0_0, TIMEPART1_0, TIMEPART2_0, TIMEPART3_0, DAYS_0, TIMEPART0_1, TIMEPART1_1, TIMEPART2_1, TIMEPART3_1, DAYS_1, ...]`
		- TIMEPART: An unsigned 32bit value that stores the trigger time in seconds
		- DAYS: An 8bit binary set that indicates which days the alarm should trigger
		- The alarms are listed in order every 5 byte

### INSPECT
- ID: 4
- OP: GET
	- Request layout: `[INDEX]`
		- INDEX: The index of the alarm to be inspected
	- Response layout: `[COMMAND (through the entire body)]`
		- COMMAND: The trigger command of the selected alarm
			- Max length is determined by the command structure


## Device Admin task
- Task id = 4
- Device name max length = 16 bytes

### DEVICEINFO
- ID: 0
- OP: GET
	- Request layout: No data required
	- Response layout: `[DEVICE_NAME (16 bytes), CURRENT_TIME_STRING (32 bytes), FREE_HEAP (4 bytes)]`
		- DEVICE_NAME: The name of the device as a string
		- CURRENT_TIME_STRING: The current system time as a string in the format of "YYYY. MM. DD. - HH:MM:SS"
		- FREE_HEAP: An unsigned 32bit value representing the free heap size in bytes

### PROJECTINFO
- ID: 1
- OP: GET
	- Request layout: No data required
	- Response layout: `[IDF_VERSION (32 bytes), PROJECT_NAME (32 bytes), PROJECT_VERSION (32 bytes)]`
		- IDF_VERSION: The version of the ESP-IDF framework used
		- PROJECT_NAME: The name of the project
		- PROJECT_VERSION: The version of the project, using git versioning

### SETNAME
- ID: 2
- OP: SET
	- Request layout: `[DEVICE_NAME (16 bytes)]`
		- DEVICE_NAME: The new name for the device as a string (forcibly null-terminated)
	- Response layout: A plaintext status


## MQTT task Internal API
- Task id = 3
- The API treats any RES or ERR commands as responses to to commands arrived through MQTT
	- Those with a TID > 0 are responses to DirectMethod calls, and the TID is used to set the $rid field in the response topic
	- Those with a TID = 0 are responses to C2D calls

### SEND_REPORTED
- ID: 0
- OP: SET
- Will skip triggering the ReportedStateCallback and directly send the given state to the broker
	- Request layout: A maximum B_COMMAND_BODY_SIZE long buffer containing the reported state JSON string
	- Response layout: No response data

### COMPILE_AND_SEND_REPORTED
- ID: 1
- OP: SET
- Will trigger the call of the ReportedStateCallback to get the latest reported state, and then send it to the broker
	- Request layout: No data required
	- Response layout: No response data

### SET_REPORT_INTERVAL
- ID: 2
- OP: SET
	- Request layout: `[REPORT_INTERVAL (4 bytes)]`
		- REPORT_INTERVAL: The new report interval in milliseconds
			- Setting it to 0 will turn off periodic reporting
			- The minimum allowed interval is 30 seconds, setting it below that will set it to 30 seconds
	- Response layout: No response data
