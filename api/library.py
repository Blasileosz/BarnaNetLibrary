import socket

SerializeDWORD = lambda n, signed = False : [int(i) for i in n.to_bytes(4, byteorder='big', signed=signed)]
SerializeWORD = lambda n, signed = False : [int(i) for i in n.to_bytes(2, byteorder='big', signed=signed)]
DeserializeDWORD = lambda b, signed = False : int.from_bytes(b, byteorder='big', signed=signed)
DeserializeWORD = lambda b, signed = False : int.from_bytes(b, byteorder='big', signed=signed)

B_COMMAND_OP_MASK = 0b11000000
B_COMMAND_OP_SET = 0b00000000
B_COMMAND_OP_GET = 0b01000000
B_COMMAND_OP_RES = 0b10000000
B_COMMAND_OP_ERR = 0b11000000

B_COMMAND_ID_MASK = 0b00111111

B_PLACEHOLDER_FROM = 0 # No need to send the from field in the tcp message
B_PLACEHOLDER_TID = 0 # No need to send the transmission ID field in the tcp message

B_COMMAND_STRUCT_SIZE = 128

class Command():
	_data: bytearray

	def __init__(self, data: bytes = None):
		if data is None:
			self._data = bytearray(B_COMMAND_STRUCT_SIZE)
		elif len(data) == B_COMMAND_STRUCT_SIZE:
			self._data = bytearray(data)
		else:
			raise ValueError("Data doesn't match the command size")

	def SetDest(self, dest):
		self._data[1] = dest

	def GetDest(self) -> int:
		return self._data[1]

	def SetHeader(self, op, id):
		if (op & B_COMMAND_OP_MASK) != op and (id & B_COMMAND_ID_MASK) != id:
			raise ValueError("Invalid header values")
		self._data[2] = op | id

	def GetHeaderOP(self) -> int:
		return self._data[2] & B_COMMAND_OP_MASK
	
	def GetHeaderID(self) -> int:
		return self._data[2] & B_COMMAND_ID_MASK

	def SetBodyByte(self, offset: int, value: int):
		#if not (0 <= value <= 255):
			#raise ValueError("Byte value must be between 0 and 255")
		
		self._data[4 + offset] = value

	def SetBodyWord(self, offset: int, value: int):
		#if not (0 <= value <= 65535):
			#raise ValueError("Word value must be between 0 and 65535")
		
		self._data[4 + offset : 4 + offset + 2] = SerializeWORD(value)

	def SetBodyDword(self, offset: int, value: int):
		#if not (-2147483648 <= value <= 2147483647):
			#raise ValueError("Dword value must be between -2147483648 and 2147483647")

		self._data[4 + offset : 4 + offset + 4] = SerializeDWORD(value)

	def CopyBodyCommand(self, offset: int, command):
		self._data[4 + offset : 4 + offset + B_COMMAND_STRUCT_SIZE - 4 - offset] = command.to_bytes()[:B_COMMAND_STRUCT_SIZE - 4 - offset]

	def SetBodyBytes(self, offset: int, data: bytearray):
		if len(data) + offset > B_COMMAND_STRUCT_SIZE - 4:
			raise ValueError("Data exceeds command body size")
		
		self._data[4 + offset : 4 + offset + len(data)] = data

	def GetBodyBytes(self) -> bytearray:
		return self._data[4:]

	def to_bytes(self) -> bytes:
		return bytes(self._data)
	
	# Represent as hex string
	def __repr__(self) -> str:
		return str(self._data.hex())


class Connection():
	socket = None

	_ip: str = ""
	_port: int = 0
	
	def __init__(self, ip: str, port: int):
		self._ip = ip
		self._port = port

	def __del__(self):
		if self.socket is not None:
			self.Disconnect()

	def Connect(self, timeout = 10):
		self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		self.socket.settimeout(timeout)
		self.socket.connect((self._ip, self._port))
		return self.socket
	
	def Disconnect(self):
		if self.socket is not None:
			self.socket.close()

	def IsAlive(self) -> bool:
		data = self.socket.recv(1, socket.MSG_PEEK | socket.MSG_DONTWAIT)
		# Empty means the connection is closed
		return data == b''

	def SendCommand(self, command: Command) -> None:
		self.socket.sendall(command.to_bytes())
	
	def ReceiveResponse(self) -> bytes:
		response = self.socket.recv(B_COMMAND_STRUCT_SIZE)
		return response

	def SendAndReceive(self, command: Command) -> bytes:
		self.SendCommand(command)
		return self.ReceiveResponse()

# TODO: Could create a universal Command parsing mechanism
#	The problem is, the service class would have to define a command id to command name connecting table

# Only super-class
# Contains Command specific declarations
# Function name constraints:
# 	Build_<OP>_<CommandName>
# 	Parse_<OP>_<request OP>_<CommandName>
class Service:

	# If command doesn't have custom parsing, call this
	@staticmethod
	def Parse_RES(command: Command) -> str:
		return str(command.GetBodyBytes())

	@staticmethod
	def Parse_ERR(command: Command) -> str:
		return str(command.GetBodyBytes())
