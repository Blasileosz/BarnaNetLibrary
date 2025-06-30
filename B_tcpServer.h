#pragma once

#define B_TASKID_TCP 1

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_system.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>

#include "B_SECRET.h"
#include "B_BarnaNetCommand.h"

// Time in miliseconds to wait for a task to reply
#define B_TCP_REPLY_TIMEOUT 1000

struct B_TcpTaskParameter {
	B_addressMap_t* addressMap;
};

// Inits the TCP server
// Returns the server socket, but returns 0 if fails
// - Private function
// - !Runs in the TCP task
// static int B_InitTCPServer()

// Dispatches the commands sent via TCP
// - Private function
// - !Runs in the TCP task
// static void B_HandleTCPMessage(const B_command_t* const command, B_command_t* const responseCommand, B_addressMap_t* addressMap)

// Send message to sock
// Arguments: (int sock, const char *const sendBuffer, size_t bufferSize)
// - Private function
// - !Runs in the TCP task
// static void B_TCPSendMessage(int sock, const char *const sendBuffer, size_t bufferSize)

// Listens for TCP messages
// - Blocking function
// - !Runs in the TCP task
// - Expected parameter: B_TcpTaskParameter struct
void B_TCPTask(void* pvParameters);
