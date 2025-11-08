from library import *

class AlarmService(Service):

	B_COMMAND_DEST_ALARM = 2

	B_ALARM_COMMAND_INSERT = 1 # Only set
	B_ALARM_COMMAND_REMOVE = 2 # Only set
	B_ALARM_COMMAND_LIST = 3 # Only get
	B_ALARM_COMMAND_INSPECT = 4 # Only get

	B_MONDAY = 0b00000010
	B_TUESDAY = 0b00000100
	B_WEDNESDAY = 0b00001000
	B_THURSDAY = 0b00010000
	B_FRIDAY = 0b00100000
	B_SATURDAY = 0b01000000
	B_SUNDAY = 0b00000001
	B_WEEKDAYS = 0b00111110
	B_WEEKENDS = 0b01000001
	B_EVERYDAY = 0b01111111

	B_ALARM_TRIGGER_SUNRISE = 0xFFFFFFFF
	B_ALARM_TRIGGER_SUNSET = 0xFFFFFFFF - 1

	@staticmethod
	def Build_SET_Insert(hour: int, minute: int, second: int, days: int, insertCommand: Command) -> Command:
		"""
		Insert a new alarm.
		The days argument is a bitmask of the days the alarm should trigger on.
			- Where sunday is 00000001, monday is 00000010 and saturday is 01000000.
		Trigger at sunrise or sunset.
			- To trigger at sunrise, set hours: 1193046, minutes: 28, seconds: 15
			- To trigger at sunset, set hours: 1193046, minutes: 28, seconds: 14
		For the trigger command paste the hex format from another tab
		"""
		# if not (0 <= hour <= 23) or not (0 <= minute <= 59) or not (0 <= second <= 59):
		# 	raise ValueError("Invalid time specified")
		
		if not (0 <= days <= 0xFF):
			raise ValueError("Days bitmask must be between 0 and 255")

		command = Command()

		timepart = AlarmService.GetTimepart(hour, minute, second)
		
		command.SetDest(AlarmService.B_COMMAND_DEST_ALARM)
		command.SetHeader(B_COMMAND_OP_SET, AlarmService.B_ALARM_COMMAND_INSERT)
		command.SetBodyDword(0, timepart)
		command.SetBodyByte(4, days)
		command.CopyBodyCommand(5, insertCommand)

		return command
	
	@staticmethod
	def Build_SET_Remove(index: int) -> Command:
		"""
		Remove an existing alarm by index
		"""
		if not (0 <= index <= 255):
			raise ValueError("Index must be between 0 and 255")

		cmd = Command()
		cmd.SetDest(AlarmService.B_COMMAND_DEST_ALARM)
		cmd.SetHeader(B_COMMAND_OP_SET, AlarmService.B_ALARM_COMMAND_REMOVE)
		cmd.SetBodyByte(0, index)
		return cmd

	@staticmethod
	def Build_GET_List() -> Command:
		"""
		Returns a list of alarms with their index, timepart, and days.
		Doesn't include the alarm's trigger command. For that please use the Inspect command.
		"""
		cmd = Command()
		cmd.SetDest(AlarmService.B_COMMAND_DEST_ALARM)
		cmd.SetHeader(B_COMMAND_OP_GET, AlarmService.B_ALARM_COMMAND_LIST)
		return cmd

	@staticmethod
	def Parse_RES_GET_List(command: Command) -> str:
		body = command.GetBodyBytes()
		count = body[0]
		parsedCommand = ""
		offset = 1
		for _ in range(count):
			index = body[offset]
			timepart = DeserializeDWORD(body[offset : offset + 5])
			days = body[offset + 5]

			parsedCommand += f"Alarm #{index} triggers at: {AlarmService.ParseTimepart(timepart)} on {AlarmService.ParseDays(days)}\n"

			offset += 5
		return parsedCommand

	@staticmethod
	def Build_GET_Inspect(index: int) -> Command:
		"""
		Get detailed info about an alarm by index
		"""
		if not (0 <= index <= 255):
			raise ValueError("Index must be between 0 and 255")

		cmd = Command()
		cmd.SetDest(AlarmService.B_COMMAND_DEST_ALARM)
		cmd.SetHeader(B_COMMAND_OP_GET, AlarmService.B_ALARM_COMMAND_INSPECT)
		cmd.SetBodyByte(0, index)
		return cmd
	
	@staticmethod
	def Parse_RES_GET_Inspect(command: Command) -> str:
		body = command.GetBodyBytes()
		triggerCommand = Command()
		triggerCommand.SetBodyBytes(0, body)

		return "\nTrigger Command (hex): " + repr(triggerCommand)

	@staticmethod
	def GetLocalTimepart():
		from datetime import datetime, timezone, timedelta

		# Adjust for docker tz
		td = timedelta(hours=2)
		tz = timezone(td)

		now = datetime.now(tz)
		return now.hour * 3600 + now.minute * 60 + now.second

	@staticmethod
	def GetTimepart(hours: int, minutes: int, seconds: int):
		return hours * 3600 + minutes * 60 + seconds

	@staticmethod
	def ParseTimepart(timepart: int) -> tuple:
		hours = timepart // 3600
		minutes = (timepart % 3600) // 60
		seconds = timepart % 60
		return hours, minutes, seconds

	@staticmethod
	def ParseDays(days: int) -> list:
		if days == B_ALARM_TRIGGER_SUNRISE:
			return ["Sunrise"]
		elif days == B_ALARM_TRIGGER_SUNSET:
			return ["Sunset"]

		dayNames = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"]
		activeDays = []
		for i in range(7):
			if days & (1 << i):
				activeDays.append(dayNames[i])
		return activeDays
