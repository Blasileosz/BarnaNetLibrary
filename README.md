# BarnaNetLibrary

## TODO
- [ ] Alarms
	- [ ] Define an alarm struct and of those an array that can be filled through tcp
- [ ] NVS: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html#application-example
- [ ] Test sunrise
- [ ] Azure IoT https://github.com/espressif/esp-azure/tree/master is deprecated, see my Github stars
- [ ] Manual connect to WIFI using the device as an AP and a web interface
- [ ] Format everything

## BarnaNet Protocol
For definition, see [B_BarnaNetCommand.h](/B_BarnaNetCommand.h)
- The protocol lies on a command structure that is used to communicate to the board and inside the board between tasks
- The command structure is aligned to one byte, this allows it to be copied to straight from the the TCP receive buffer
- A command has 3 identifiers:
	- Operation (OP): defines the type of the command, can be SET, GET, RESPONSE or ERROR
	- Destination (DEST): specifies which subsystem the command is heading to or coming from (for example, a DEST value of 1 will be forwarded to the led controller task)
	- ID: only used by the subsystem the command was sent to (for example, in the led controller task context, a command id of 1 specifies color change)
- The structure
	- Header: Contains the OP and the DEST. 1 byte
	- ID: 1 byte
	- StripID: unused, may be deleted later
	- Data: 125 raw bytes

## TCP Server
For definition, see [B_tcpServer.h](/B_tcpServer.h)
- Multi client server
- The B_TCPTask function should be run in a task with at least 4096 words of stack depth (a word is the width of the stack portSTACK_TYPE)
- For the task parameter, a function pointer is expected, that handles the incoming and outgoing commands
- TCP server does not currently bind to IPv6 (clients are still supported through IPv6)

## TIME
For definition, see [B_time.h](/B_time.h)
- [EPS-IDF time documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/system_time.html)
- [ ] Add types for storing only time (hh:mm) and date (yyyy. mm. dd. hh:mm)
- Includes an NTP client
- Includes Sunrise and Sunset calculation function

## Alarm
For definition, see [B_alarm.h](/B_alarm.h)
- [EPS-IDF timer and alarm documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gptimer.html)
- Creates a timer that is ticking up at 1MHz
- Attaches an alarm that triggers an ISR when the timer's target value is reached
