#include "B_BarnaNetCommand.h"

// Command helper functions

// Copy string into body, zero terminates it
void B_FillCommandBodyString(B_command_t* const command, const char* const string)
{
	if (command == NULL || string == NULL || strlen(string) + 1 > B_COMMAND_BODY_SIZE)
		return;
	
	memcpy(command->body, string, strlen(string));
}

// Concise way to send small status replies
// Arguments:
// - addressMap: The address map containing the queues
// - command: The original command to reply to
// - thisTaskID: The TaskID of the task sending the reply
// - commandOP: The operation of the reply (B_COMMAND_OP_...)
// - string: The string to put into the body of the reply
bool B_SendStatusReply(B_addressMap_t* const addressMap, const B_command_t* const command, uint8_t thisTaskID, uint8_t commandOP, const char* const string) {

	if (addressMap == NULL || command == NULL || thisTaskID == 0 || string == NULL)
		return false;

	unsigned int destFlags = 0;
	QueueHandle_t destQueue = B_GetAddressAndFlags(addressMap, command->from, &destFlags);
	if (destQueue == B_ADDRESS_MAP_INVALID_QUEUE)
		return false;

	// Don't send reply if the task doesn't want it
	if (destFlags & B_TASK_FLAG_NO_REPLY)
		return true;

	B_command_t response = { 0 };
	B_FillCommandBodyString(&response, string);

	// Fill header
	response.from = thisTaskID;
	response.dest = command->from;
	response.header = commandOP | B_COMMAND_ID(command->header);
	response.transmissionID = command->transmissionID;

	if (xQueueSend(destQueue, &response, 0) != pdPASS)
		return false;

	return true;
}

// Reply from the dest task to the command's sender task
// Arguments:
// - addressMap: The address map containing the queues
// - command: The original command to reply to
// - responseCommand: The command to send as a reply, the header, from, dest and transmissionID fields will be overwritten but the body should be filled
// - thisTaskID: The TaskID of the task sending the reply
bool B_SendReplyCommand(B_addressMap_t* const addressMap, const B_command_t* const command, B_command_t* const responseCommand, uint8_t thisTaskID)
{
	if (addressMap == NULL || command == NULL || responseCommand == NULL || thisTaskID == 0)
		return false;

	unsigned int destFlags = 0;
	QueueHandle_t destQueue = B_GetAddressAndFlags(addressMap, command->from, &destFlags);
	if (destQueue == B_ADDRESS_MAP_INVALID_QUEUE)
		return false;

	// Don't send reply if the task doesn't want it
	if (destFlags & B_TASK_FLAG_NO_REPLY)
		return true;

	// Fill header
	responseCommand->from = thisTaskID;
	responseCommand->dest = command->from;
	responseCommand->header = B_COMMAND_OP_RES | B_COMMAND_ID(command->header);
	responseCommand->transmissionID = command->transmissionID;

	if (xQueueSend(destQueue, responseCommand, 0) != pdPASS)
		return false;

	return true;
}

// Relay commands from ingress tasks to the dest task
// Fills the FROM and transmissionID fields
// Arguments:
// - addressMap: The address map containing the queues
// - command: The command to relay, the from and transmissionID fields will be overwritten
// - thisTaskID: The TaskID of the task sending the command
// - transmissionID: The unique ID for this command
bool B_RelayCommand(B_addressMap_t* const addressMap, B_command_t* const command, uint8_t thisTaskID, uint8_t transmissionID)
{
	if (addressMap == NULL || command == NULL)
		return false;

	unsigned int destFlags = 0;
	QueueHandle_t destQueue = B_GetAddressAndFlags(addressMap, command->dest, &destFlags);
	if (destQueue == B_ADDRESS_MAP_INVALID_QUEUE)
		return false;

	// Don't relay the command if the task is a comms task
	if (destFlags & B_TASK_FLAG_ONLY_REPLY)
		return false;

	command->from = thisTaskID;
	command->transmissionID = transmissionID;

	if (xQueueSend(destQueue, command, 0) != pdPASS)
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
	if (addressMap == NULL || addressMap->addressList == NULL || ID == 0 || queueHandle == NULL)
		return false;

	// Invalid insert index
	if (insertIndex >= addressMap->mapSize)
		return false;

	addressMap->addressList[insertIndex].queueID = ID;
	addressMap->addressList[insertIndex].flags = flags;
	addressMap->addressList[insertIndex].queueHandle = queueHandle;
	return true;
}

// Get the queue handle from the address map by the corresponding TaskID
// Returns B_ADDRESS_MAP_INVALID_QUEUE if queue with the given TaskID was not found
QueueHandle_t B_GetAddress(const B_addressMap_t* const addressMap, int ID)
{
	if (addressMap == NULL || addressMap->addressList == NULL || ID == 0)
		return B_ADDRESS_MAP_INVALID_QUEUE;

	for (int i = 0; i < addressMap->mapSize; i++)
	{
		if (addressMap->addressList[i].queueID == ID)
			return addressMap->addressList[i].queueHandle;
	}

	return B_ADDRESS_MAP_INVALID_QUEUE;
}

// Get the queue handle and the routing flags from the address map by the corresponding TaskID
// Returns B_ADDRESS_MAP_INVALID_QUEUE if queue with the given TaskID was not found
QueueHandle_t B_GetAddressAndFlags(const B_addressMap_t* const addressMap, int ID, unsigned int* const flagsOut)
{
	if (addressMap == NULL || addressMap->addressList == NULL || ID == 0 || flagsOut == NULL)
		return B_ADDRESS_MAP_INVALID_QUEUE;

	for (int i = 0; i < addressMap->mapSize; i++)
	{
		if (addressMap->addressList[i].queueID == ID) {

			if (flagsOut != NULL)
				*flagsOut = addressMap->addressList[i].flags;
				
			return addressMap->addressList[i].queueHandle;
		}
	}

	return B_ADDRESS_MAP_INVALID_QUEUE;
}
