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
	- Request layout: `No data required`
	- Response layout: `[ALARM_COUNT, TIMEPART0_0, TIMEPART1_0, TIMEPART2_0, TIMEPART3_0, DAYS_0, TIMEPART0_1, TIMEPART1_1, TIMEPART2_1, TIMEPART3_1, DAYS_1, ...]`
		- TIMEPART: An unsigned 32bit value that stores the trigger time in seconds
		- DAYS: An 8bit binary set that indicates which days the alarm should trigger
		- The alarms are listed in order every 5 byte

### INSPECT
- ID: 4
- OP: GET
	- Request layout: `[INDEX]`
		- INDEX: The index of the alarm to be inspected
	- Response layout: `[COMMAND * B_COMMAND_BODY_SIZE]`
		- COMMAND: The trigger command of the selected alarm
			- Max length is determined by the command structure
