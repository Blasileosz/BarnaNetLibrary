# BarnaNetLibrary

## TODO
- [x] Alarms
- [ ] Test sunrise
- [ ] Home Assistant integration
- [ ] NVS: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html#application-example
- [ ] Azure IoT https://github.com/espressif/esp-azure/tree/master is deprecated, see my Github stars
- [ ] mDNS or a custom discovery protocol to see other BarnaNet devices
- [ ] Manual connect to WIFI using the device as an AP and a web interface
- [ ] Clean up

## Project structure
- Some systems are implemented using FreeRTOS tasks
- Each task is an infinite loop running in parallel
- Every system task should have an ID (max 8bit) for communication
- Task communication
	- The `B_addressMap_t` structure stores the `B_address_t` structures
	- These addresses are key-value pairs (and flags) to match the Task id to its queue handle
	- The address map can be initialized with the `B_AddressMapInit` function and deleted with the `B_AddressmapCleanup`
	- To insert a task, use the `B_InsertAddress`
	- To find the queue handle with an ID, use `B_GetAddress` or `B_GetAddressAndFlags`

## BarnaNet Protocol
- For the definition, see [B_BarnaNetCommand.h](/B_BarnaNetCommand.h)
- For the API endpoints, see [Protocol.md](/Protocol.md)
- The protocol lies on a command structure that is used to communicate to the board and inside the board between tasks
- The command structure is aligned to one byte; this allows it to be copied to straight from the TCP receive buffer
- A command has 4 identifiers:
	- Origin (FROM): specifies which system the command is coming from
	- Destination (DEST): specifies which subsystem the command is heading to
	- Operation (OP): defines the type of the command, can be SET, GET, RESPONSE or ERROR
	- ID: uniquely defines a function for the task
- The structure
	- The size is defined as B_COMMAND_STRUCT_SIZE
	- FROM: 1 byte
	- DEST: 1 byte
	- Header: 1 byte; Contains the OP and the ID
	- Body size is B_COMMAND_BODY_SIZE (or B_COMMAND_STRUCT_SIZE - 3)
- There are helper function to help with filling or reading the command struct
	- Filling the header: `B_FillCommandHeader`
	- Filling the body: `B_FillCommandBodyString`, `B_FillCommandBody_BYTE`, `B_FillCommandBody_WORD`, `B_FillCommandBody_DWORD`
	- Reading the body: `B_ReadCommandBody_BYTE`, `B_ReadCommandBody_WORD`, `B_ReadCommandBody_DWORD`

## TCP Server
For definition, see [B_tcpServer.h](/B_tcpServer.h)
- Task function: `B_TCPTask`
- For the task parameter, the given `B_TcpTaskParameter` struct should be filled
- Receives and forwards commands to the appropriate task, waits for reply from the task and sends that back to the client
- The task should be run with at least 4096 words of stack depth (a word is the width of the stack portSTACK_TYPE)
- The server can handle multiple clients in parallel
- The server binds to all interfaces on the port specified in the menuconfig
- The server doesn't currently bind to IPv6 (clients are still supported through IPv6)

## TIME
For definition, see [B_time.h](/B_time.h)
- [ESP-IDF time documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/system_time.html)
- Defines a 32bit type `B_timepart_t` that holds the hours, minutes and seconds of a timestamp
- Includes an SNTP client to sync the system time
	- `B_SyncTime` should be called in the main task after connected to WIFI
	- The timezone and the SNTP server are defined here as well
	- Getting the system time can be done using regular C functions
- Includes Sunrise and Sunset calculation function
	- To ease the strain, two prebaked tables are also defined
	- To get the data, I used the [NOAA calculator](https://gml.noaa.gov/grad/solcalc/), exported the [table](https://gml.noaa.gov/grad/solcalc/table.php?lat=47.896076&lon=20.380324&year=2025), removed DST in excel and parsed it
	- Or use the [python script](/utils/bakeSun.py) to cook up the table

## Alarm
For definition, see [B_alarm.h](/B_alarm.h)
- [ESP-IDF timer and alarm documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gptimer.html)
- Task function: `B_AlarmTask`
- For the task parameter, the given `B_AlarmTaskParameter` struct should be filled
- Calculates the next alarm to be triggered, starts its timer and when the timer reaches the delta, it fires an ISR
- The ISR then sends a command to the task to trigger the alarm's trigger-command
- The task then forwards the trigger-command to its destination
- To add, remove, list or inspect alarms, please see the API
- The alarm container has a finite size as specified in the menuconfig

## WIFI
For definition, see [B_wifi.h](/B_wifi.h)
- Simple station WIFI driver
- WIFI credentials should be defined as `B_AP_SSID` and `B_AP_PASS` in the `B_SECRET.h` file (see [B_SECRET.txt](/B_SECRET.txt) for example)
- To connect, call the `B_WifiConnect` function

## COLOR
For definition, see [B_colorUtil.h](/B_colorUtil.h)
- Defines an RGB structure and several color manipulation functions
