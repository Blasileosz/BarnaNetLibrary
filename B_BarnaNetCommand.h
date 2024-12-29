#pragma once

#include <stdio.h>
#include "B_colorUtil.h"

// Aligned to one byte, which allows it to be copied directly from the received TCP buffer
typedef struct {
	uint8_t header; // Command OP and DEST
	uint8_t ID; // Command ID
	uint8_t stripID; // TODO: add feature

	uint8_t data[125];
} B_command_t;

static_assert(sizeof(B_command_t) == 128);
static_assert(_Alignof(B_command_t) == 1);

// COMMAND HEADER:	0b11222222
// COMMAND ID:		0b33333333
// 1: Command operation (Get, Set, Response, Error)
// 2: Command destination (LED controller, Alarm system, etc.)
// 3: Command id (Unique identifier for a command given the operation and destination)

#define B_COMMAND_OP_MASK (uint8_t) 0b11000000
#define B_COMMAND_OP_SET (uint8_t) 0b00000000
#define B_COMMAND_OP_GET (uint8_t) 0b01000000
#define B_COMMAND_OP_RES (uint8_t) 0b10000000
#define B_COMMAND_OP_ERR (uint8_t) 0b11000000

#define B_COMMAND_DEST_MASK (uint8_t)0b00111111

#define B_COMMAND_OP(header) (header & B_COMMAND_OP_MASK)
#define B_COMMAND_DEST(header) (header & B_COMMAND_DEST_MASK)

// ERROR TYPES (should be sent in the data section)
#define B_COMMAND_ERR_CLIENT 0b00000000 // Bad request
#define B_COMMAND_ERR_INTERNAL 0b10000000 // Server error
