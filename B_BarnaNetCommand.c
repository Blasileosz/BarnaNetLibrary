#include "B_BarnaNetCommand.h"

// Command helper functions

// TODO: The command cannot be null, but there is no check in place

void B_FillCommandHeader(B_command_t* const command, uint8_t from, uint8_t dest, uint8_t operation, uint8_t ID)
{
	command->from = from;
	command->dest = dest;
	operation = operation & B_COMMAND_OP_MASK;
	ID = ID & B_COMMAND_ID_MASK;
	command->header = operation | ID;
}

void B_FillCommandBodyString(B_command_t* const command, const char* const string)
{
	//static_assert(strlen(errorMessage) > len(command.body));
	memcpy(command->body, string, strlen(string));
}

bool B_SendStatusReply(B_addressMap_t* addressMap, uint8_t from, uint8_t dest, uint8_t commandOP, uint8_t commandID, const char* const string) {

	unsigned int destFlags = 0;
	QueueHandle_t destQueue = B_GetAddressAndFlags(addressMap, dest, &destFlags); // TODO: Validate destQueue

	// Don't send reply if the task doesn't want it
	if (destFlags & B_TASK_FLAG_NO_STATUS_REPLY)
		return true;

	B_command_t response = { 0 };
	B_FillCommandHeader(&response, from, dest, commandOP, commandID);
	B_FillCommandBodyString(&response, string);

	if (xQueueSend(destQueue, &response, 0) != pdPASS)
		return false;

	return true;
}


// Queues

bool B_AddressMapInit(B_addressMap_t* addressMap, int size)
{
	addressMap->addressList = calloc(size, sizeof(B_address_t));
	if (addressMap->addressList == NULL)
		return false;

	addressMap->mapSize = size;
	return true;
}

void B_AddressmapCleanup(B_addressMap_t* addressMap)
{
	if (addressMap->addressList == NULL)
		return;
	
	// Delete the queues
	for (int i = 0; i < addressMap->mapSize; i++) {
		if (addressMap->addressList[i].queueHandle != NULL) {
			vQueueDelete(addressMap->addressList[i].queueHandle);
		}
	}

	free(addressMap->addressList);
	addressMap->addressList = NULL;
	addressMap->mapSize = 0;
}

bool B_InsertAddress(B_addressMap_t* const addressMap, int insertIndex, int ID, QueueHandle_t queueHandle, unsigned int flags)
{
	// Invalid insert index
	if (insertIndex >= addressMap->mapSize)
		return false;

	addressMap->addressList[insertIndex].queueID = ID;
	addressMap->addressList[insertIndex].flags = flags;
	addressMap->addressList[insertIndex].queueHandle = queueHandle;
	return true;
}

QueueHandle_t B_GetAddress(const B_addressMap_t* const addressMap, int ID)
{
	for (int i = 0; i < addressMap->mapSize; i++)
	{
		if (addressMap->addressList[i].queueID == ID)
			return addressMap->addressList[i].queueHandle;
	}

	return (void*)(-1);
}

QueueHandle_t B_GetAddressAndFlags(const B_addressMap_t* const addressMap, int ID, unsigned int* const flagsOut)
{
	for (int i = 0; i < addressMap->mapSize; i++)
	{
		if (addressMap->addressList[i].queueID == ID) {

			if (flagsOut != NULL)
				*flagsOut = addressMap->addressList[i].flags;
				
			return addressMap->addressList[i].queueHandle;
		}
	}

	return (void*)(-1);
}
