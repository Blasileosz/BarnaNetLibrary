#pragma once

#include <lwip/sockets.h> // For endianness changing macros
#include <stdio.h>

#include "B_colorUtil.h"

#define B_COMMAND_STRUCT_SIZE 128
#define B_COMMAND_BODY_SIZE (B_COMMAND_STRUCT_SIZE - 3)

// Aligned to one byte, which allows it to be copied directly from the received TCP buffer
typedef struct {
	uint8_t from; // TASKID that sent the command
	uint8_t dest; // TASKID to send the command to
	uint8_t header; // Command OP and ID (see below)
	// TODO: transmition ID (more in the TODO section of the README)

	uint8_t body[B_COMMAND_BODY_SIZE];
} B_command_t;

// TODO: Should the command ID be the ERR and RES code? like 400, 500 and 200, 100 (right now, the request is copied back, so the reply can be distinguished)
// COMMAND HEADER:	0b11222222
// 1: Command operation (Get, Set, Response, Error)
// 2: Command ID (Unique identifier for a command given the operation and destination) 64 possible IDs

// COMMAND FROM and DEST
// Every system should define a TASKID in its .h file
// For example B_TASKID_TCP, B_TASKID_ALARM

static_assert(sizeof(B_command_t) == B_COMMAND_STRUCT_SIZE);
static_assert(_Alignof(B_command_t) == 1);

#define B_COMMAND_OP_MASK ((uint8_t)0b11000000)
#define B_COMMAND_OP_SET ((uint8_t)0b00000000)
#define B_COMMAND_OP_GET ((uint8_t)0b01000000)
#define B_COMMAND_OP_RES ((uint8_t)0b10000000)
#define B_COMMAND_OP_ERR ((uint8_t)0b11000000)

#define B_COMMAND_ID_MASK ((uint8_t)0b00111111)

#define B_COMMAND_OP(header) (header & B_COMMAND_OP_MASK)
#define B_COMMAND_ID(header) (header & B_COMMAND_ID_MASK)

// TODO: ERROR TYPES
#define B_COMMAND_ERR_CLIENT 0b00000000 // Bad request
#define B_COMMAND_ERR_INTERNAL 0b10000000 // Server error

// Command helper functions
void B_FillCommandHeader(B_command_t* const command, uint8_t from, uint8_t dest, uint8_t operation, uint8_t ID);
// TODO: bounds check?
void B_FillCommandBodyString(B_command_t* const command, const char* const string);
inline void B_FillCommandBody_BYTE(B_command_t* const command, int byteOffset, uint8_t value) { command->body[byteOffset] = value; }
inline void B_FillCommandBody_WORD(B_command_t* const command, int byteOffset, uint16_t value) { *((uint16_t*)&command->body[byteOffset]) = ntohs(value); }
inline void B_FillCommandBody_DWORD(B_command_t* const command, int byteOffset, uint32_t value) { *((uint32_t*)&command->body[byteOffset]) = ntohl(value); }
inline uint8_t B_ReadCommandBody_BYTE(B_command_t* const command, int byteOffset) { return command->body[byteOffset]; }
inline uint16_t B_ReadCommandBody_WORD(B_command_t* const command, int byteOffset) { return htons(*((uint16_t*)(&command->body[byteOffset]))); }
inline uint32_t B_ReadCommandBody_DWORD(B_command_t* const command, int byteOffset) { return htonl(*((uint32_t*)(&command->body[byteOffset]))); }


#define B_TASK_FLAG_NO_STATUS_REPLY 1 // Task doesn't need status results (for example, when an alarm triggers, the alarm task doesn't need a reply from the led controller)

// ID-Queue key-value pair to translate DEST and FROM values to queues
// Sent to tasks instead of individual queues
typedef struct {
	int queueID;
	unsigned int flags; // Suggests how this task should be treated
	QueueHandle_t queueHandle;
} B_address_t;

// Stores the B_address_t key-value pairs
typedef struct {
	int mapSize;
	B_address_t* addressList;
} B_addressMap_t;

bool B_AddressMapInit(B_addressMap_t* addressMap, int size);
void B_AddressmapCleanup(B_addressMap_t* addressMap);
bool B_InsertAddress(B_addressMap_t* const addressMap, int insertIndex, int ID, QueueHandle_t queueHandle, unsigned int flags);

// Returns uint max if queue with the gived ID was not found
QueueHandle_t B_GetAddress(const B_addressMap_t* const addressMap, int ID);
QueueHandle_t B_GetAddressAndFlags(const B_addressMap_t* const addressMap, int ID, unsigned int* const flagsOut);

// Concise way to send small status replies
bool B_SendStatusReply(B_addressMap_t* addressMap, uint8_t from, uint8_t dest, uint8_t commandOP, uint8_t commandID, const char* const string);
