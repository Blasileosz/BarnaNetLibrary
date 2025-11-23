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

#include "B_BarnaNetCommand.h"

struct B_TCPIngressTaskParameter {
	B_addressMap_t* addressMap;
};

// Private struct for passing parameters to the egress task
struct B_TCPEgressTaskParameter {
	B_addressMap_t* addressMap;
	fd_set* socketSet;
};

// This task uses the transmissionID field of the command struct as the socket identifier
// Thus it can be used as an index in the fd_set
// Thankfully, the maximum number of sockets (FD_SETSIZE) is 64 on the ESP32
static_assert(FD_SETSIZE <= 255);

// Inits the TCP server
// Returns the server socket, but returns 0 if fails
// - Private function
// - !Runs in the TCP task
// static int B_InitTCPServer()

// Dispatches the commands sent via TCP
// Doesn't return error, instead fills the responseCommand with an error message and sends it to the egress task
// - Private function
// - !Runs in the TCP task
// static void B_HandleTCPMessage(B_addressMap_t* const addressMap, B_command_t* const command, uint8_t transmissionID)

// Send message to sock
// Arguments: (int sock, const char *const sendBuffer, size_t bufferSize)
// - Private function
// - !Runs in the TCP task
// static void B_TCPSendMessage(int sock, const char *const sendBuffer, size_t bufferSize)

// Listens for replies to the TCP messages
// - Blocking function
// - Private function
// - !Runs in the TCP Egress task
// - Expected parameter: B_TCPEgressTaskParameter struct
// static void B_TCPEgressTask(void* pvParameters)

// Listens for TCP messages
// - Blocking function
// - !Runs in the TCP Ingress task
// - Expected parameter: B_TCPIngressTaskParameter struct
void B_TCPIngressTask(void* pvParameters);
