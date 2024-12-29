#pragma once

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

// B_InitTCPServer()
// Inits the TCP server
// Returns the server socket, but returns 0 if fails
// - Private function
// - !Runs in the TCP task

// B_TCPSendMessage
// Send message to sock
// Arguments: (int sock, const char *const sendBuffer, size_t bufferSize)
// - Private function
// - !Runs in the TCP task

// Listens for TCP messages
// - Blocking function
// - !Runs in the TCP task
// - Expected function pointer: void (*handlerFunctionPointer)(const char* const messageBuffer, int messageLen, char* const responseBufferOut, int* const responseLenOut)
void B_TCPTask(void* pvParameters);
