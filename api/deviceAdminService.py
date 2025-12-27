from library import *

class DeviceAdminService(Service):

	B_COMMAND_DEST_DEVICE_ADMIN = 4

	B_DEVICEADMIN_NAME_MAX_LENGTH = 16

	B_DEVICEADMIN_COMMAND_DEVICEINFO = 0 # Only get
	B_DEVICEADMIN_COMMAND_PROJECTINFO = 1 # Only get
	B_DEVICEADMIN_COMMAND_SETNAME = 2 # Only set

	@staticmethod
	def Build_GET_DeviceInfo() -> Command:
		"""
		Get device information.
		Includes device name, current time and free heap size.
		"""

		command = Command()
		
		command.SetDest(DeviceAdminService.B_COMMAND_DEST_DEVICE_ADMIN)
		command.SetHeader(B_COMMAND_OP_GET, DeviceAdminService.B_DEVICEADMIN_COMMAND_DEVICEINFO)
		return command
	
	@staticmethod
	def Parse_RES_GET_DeviceInfo(command: Command) -> str:
		body = command.GetBodyBytes()
		offset = 0

		deviceName = body[offset : offset + 16].decode('utf-8')
		offset += 16

		currentTime = body[offset : offset + 32].decode('utf-8')
		offset += 32

		freeHeap = DeserializeDWORD(body[offset : offset + 4])
		offset += 4

		return f"Device Name: {deviceName}\nCurrent Time: {currentTime}\nFree Heap: {freeHeap} bytes"
	
	@staticmethod
	def Build_GET_ProjectInfo() -> Command:
		"""
		Get project information.
		Includes IDF version, project name and version.
		"""

		command = Command()
		
		command.SetDest(DeviceAdminService.B_COMMAND_DEST_DEVICE_ADMIN)
		command.SetHeader(B_COMMAND_OP_GET, DeviceAdminService.B_DEVICEADMIN_COMMAND_PROJECTINFO)
		return command
	
	@staticmethod
	def Parse_RES_GET_ProjectInfo(command: Command) -> str:
		body = command.GetBodyBytes()
		offset = 0

		idfVersion = body[offset : offset + 32].decode('utf-8')
		offset += 32

		projectName = body[offset : offset + 32].decode('utf-8')
		offset += 32

		version = body[offset : offset + 32].decode('utf-8')
		offset += 32

		return f"IDF Version: {idfVersion}\nProject Name: {projectName}\nVersion: {version}"
	
	@staticmethod
	def Build_SET_DeviceName(deviceName: str) -> Command:
		"""
		Set the device name.
		"""

		# Validate name length, ensure null-termination
		if len(deviceName) >= DeviceAdminService.B_DEVICEADMIN_NAME_MAX_LENGTH:
			raise ValueError(f"Device name cannot exceed {DeviceAdminService.B_DEVICEADMIN_NAME_MAX_LENGTH} bytes")

		command = Command()
		
		command.SetDest(DeviceAdminService.B_COMMAND_DEST_DEVICE_ADMIN)
		command.SetHeader(B_COMMAND_OP_SET, DeviceAdminService.B_DEVICEADMIN_COMMAND_SETNAME)
		nameBytes = deviceName.encode('utf-8')
		command.SetBodyBytes(0, nameBytes)
		return command
