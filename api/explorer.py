from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, TabbedContent, TabPane, Collapsible, Rule, Input, TextArea, Static, Label, Button, Switch
from textual.containers import Vertical, Horizontal
from enum import unique
from types import MethodType
import inspect
import library
import asyncio
import pyperclip

# Textual docs: https://textual.textualize.io/

# TODO: Try Textual workers (https://textual.textualize.io/guide/workers/)
#	- https://textual.textualize.io/guide/workers/
#	- Either need to make Connect() and SendAndReceive() async, that would make these functions a hassle to use elsewhere
#	- Or create a thread worker, that cannot update the UI directly
# TODO: Create a template class for services and instantiate them in the main compose

class APIExplorer(App):

	BINDINGS = [
		("q", "quit_app", "Quit App"),
	]

	BNfunctionDict: dict = {}
	connection: library.Connection = None

	# Collect information on the build functions from the provided services
	def BuildFunctionDict(self, services: list[type]):
		# Filter for Service sublclasses
		validServices = filter(lambda service: issubclass(service, library.Service), services)
		functionDict = {}

		for service in validServices:
			functions = inspect.getmembers(service, predicate=inspect.isfunction) # Get all function members, returns list of (name, callable)
			buildFunctions = filter(lambda member: member[0].startswith("Build_"), functions)

			# Store function info in dict
			for functionName, functionCallable in buildFunctions:
				functionArguments = inspect.signature(functionCallable).parameters.items()
				# Convert it to tuple and get the parameter's annotation, because that contains the base type
				functionArguments = [(name, param.annotation) for name, param in functionArguments]

				functionDict[functionName] = {
					"serviceType": service,
					"serviceName": service.__name__,
					"callable": functionCallable,
					"docstring": inspect.getdoc(functionCallable),
					"arguments": functionArguments
				}

		return functionDict

	# Handle the result of self.connection.Connect
	def ConnectionCallback(self, future: asyncio.Future):
		try:
			result = future.result()
			self.notify("Connected", title="Connection")
		except Exception as e:
			self.notify(f"Failed to connect: {e}", severity="error", title="Connection")

	# Run the tcp connection in a background thread to avoid blocking the event loop
	def ConnectToDevice(self):
		connectionTask = asyncio.create_task(asyncio.to_thread(self.connection.Connect))
		connectionTask.add_done_callback(self.ConnectionCallback)

		self.notify(f"Connecting to {self.connection._ip}:{self.connection._port}...", title="Connection")

	def __init__(self, BNservices: list[type], ip: str, port: int, **kwargs):
		# Defer connection until the app is mounted (so it runs in the app's event loop)
		self.connection = library.Connection(ip, port)

		self.BNfunctionDict = self.BuildFunctionDict(BNservices)
		
		super().__init__(**kwargs)  # Pass every other arg to parent

	def __del__(self):
		pass

	# Textual starting point
	def on_mount(self) -> None:
		self.ConnectToDevice()

	# Textual compose function
	def compose(self) -> ComposeResult:
		yield Header()
		yield Footer()

		with TabbedContent():
	
			# Create a tab for each build function (API endpoint)
			for functionName, functionInfo in self.BNfunctionDict.items():
				functionService = functionInfo['serviceName']
				functionNameShort = functionName.removeprefix("Build_GET_").removeprefix("Build_SET_")
				tabName = f"{functionService}:{functionNameShort}"

				# IDs cannot have colons, though it would look much cleaner
				uniqueTabID = "--".join([functionService, functionName])

				# Use double colons to separate the action and the function identifiers (used in the press handler)
				uniqueSendName = "::".join(["SEND", functionService, functionName])
				uniqueCopyName = "::".join(["COPY", functionService, functionName])
				uniqueShowName = "::".join(["SHOW", functionService, functionName])

				with TabPane(tabName, id=uniqueTabID):

					yield Static(f"Function: {functionName}", id=f"{uniqueTabID}_name")
					yield Static(f"Description: {functionInfo['docstring']}\n", id=f"{uniqueTabID}_desc")

					# Create inputs for each argument
					for argName, argType in functionInfo["arguments"]:

						if argType == int:
							yield Input(placeholder=f"Integer argument: {argName}", type="integer", id=f"{uniqueTabID}_arg_{argName}")
						elif argType == str:
							yield Input(placeholder=f"String argument: {argName}", type="text", id=f"{uniqueTabID}_arg_{argName}")
						elif argType == bool:
							yield Label(f"Boolean argument (True/False): {argName}")
							yield Switch(id=f"{uniqueTabID}_arg_{argName}")
						elif argType == library.Command:
							yield TextArea(placeholder=f"Command hex string: {argName}", id=f"{uniqueTabID}_arg_{argName}")
						else:
							yield Static(f"Unsupported argument type: {argType} for argument {argName}", id=f"{uniqueTabID}_arg_{argName}")

					with Horizontal():
						yield Button("Send", name=uniqueSendName, flat=True, variant="primary")
						yield Button("Copy Command", name=uniqueCopyName, flat=True)
						yield Button("Show Command", name=uniqueShowName, flat=True)

					with Collapsible(title="Raw command", id=f"{uniqueTabID}_commandCollapsible"):
						yield Static("Command will be shown here", id=f"{uniqueTabID}_commandBytes")

					with Collapsible(title="Response", id=f"{uniqueTabID}_responseCollapsible"):
						yield Static("Response will be shown here", id=f"{uniqueTabID}_responseBytes")
						yield Rule()
						yield Static("", id=f"{uniqueTabID}_response")

	# Call the appropriate command builder function with the gathered arguments
	# Returns the built Command object or false on error
	def CallCommandBuilder(self, functionName: str, functionService: str) -> library.Command:
		functionInfo = self.BNfunctionDict.get(functionName, None)
		if functionInfo is None or functionInfo["serviceName"] != functionService:
			self.notify("Invalid function call", title="Parsing", severity="error")
			return False

		callArguments = {}

		# Gather arguments from inputs
		for argName, argType in functionInfo["arguments"]:
			inputID = f"{functionInfo['serviceName']}--{functionName}_arg_{argName}"
			inputElement = self.query_one(f"#{inputID}")

			if argType == int:
				argValue = int(inputElement.value if inputElement.value != "" else 0)
			elif argType == str:
				argValue = str(inputElement.value)
			elif argType == bool:
				argValue = bool(inputElement.value)
			elif argType == library.Command:
				try:
					commandBytes = bytearray.fromhex(inputElement.text)
					argValue = library.Command(commandBytes)
				except Exception as e:
					self.notify(f"Failed to parse Command argument {argName}: {e}", title="Parsing", severity="error")
					return False
			else:
				self.notify(f"Unsupported argument type: {argType} for argument {argName}", title="Parsing", severity="error")
				return False

			callArguments[argName] = argValue

		# Call the builder function with the gathered arguments
		try:
			functionCallable = functionInfo["callable"]
			command = functionCallable(**callArguments)
		except Exception as e:
			self.notify(f"Command builder failed with error: {e}", title="Parsing", severity="error")
			return False
		
		return command
	
	# Handle the result of the command transaction
	def TransactionCallback(self, future: asyncio.Future):
		try:
			(responseBytes, functionName, functionService) = future.result()
		except Exception as e:
			self.notify(f"Transaction failed: {e}", severity="error", title="Transaction")
			return

		responseCommand = library.Command(responseBytes)

		uniqueTabID = "--".join([functionService, functionName])
		collapsibleElement = self.query_one(f"#{uniqueTabID}_responseCollapsible")
		responseBytesStatic = self.query_one(f"#{uniqueTabID}_responseBytes")
		responseStatic = self.query_one(f"#{uniqueTabID}_response")

		collapsibleElement.collapsed = False
		responseBytesStatic.update(responseCommand.to_bytes().hex())

		# Validate response OP
		if responseCommand.GetHeaderOP() != library.B_COMMAND_OP_RES and responseCommand.GetHeaderOP() != library.B_COMMAND_OP_ERR:
			self.notify("Invalid response received", severity="error", title="Transaction")
			return
		
		parserFunction = None

		# Check for error or parser function (assume functionService is correct)
		if responseCommand.GetHeaderOP() == library.B_COMMAND_OP_ERR:
			parserFunctionName = functionName.replace("Build_", "Parse_ERR_")
			parserFunction = getattr(functionService, parserFunctionName, None)

			if parserFunction is None: # Fallback to generic parser
				parserFunction = library.Service.Parse_ERR
		
		elif responseCommand.GetHeaderOP() == library.B_COMMAND_OP_RES:
			parserFunctionName = functionName.replace("Build_", "Parse_RES_")
			parserFunction = getattr(functionService, parserFunctionName, None)

			if parserFunction is None: # Fallback to generic parser
				parserFunction = library.Service.Parse_RES

		parsedResponse = parserFunction(responseCommand)
		responseStatic.update(f"Response:\n{str(parsedResponse)}")

	# Separate thread function, because the callback needs the functionName and functionService for updating the UI
	def CommandTransactionThread(self, command: library.Command, functionName: str, functionService: str):
		responseBytes = self.connection.SendAndReceive(command)
		return responseBytes, functionName, functionService

	# Textual button press handler, called when any button is pressed
	def on_button_pressed(self, event: Button.Pressed) -> None:

		buttonNameParts = event.button.name.split("::")

		if buttonNameParts[0] == "SEND":
			functionName = buttonNameParts[2]
			functionService = buttonNameParts[1]

			command = self.CallCommandBuilder(functionName, functionService)
			if command is False:
				return

			# Send the command in a background thread to avoid blocking the event loop
			transactionTask = asyncio.create_task(asyncio.to_thread(self.CommandTransactionThread, command, functionName, functionService))
			transactionTask.add_done_callback(self.TransactionCallback)

		elif buttonNameParts[0] == "COPY":
			functionName = buttonNameParts[2]
			functionService = buttonNameParts[1]

			command = self.CallCommandBuilder(functionName, functionService)
			if command is False:
				return
			
			# TODO: test https://pyperclip.readthedocs.io/en/latest/index.html#not-implemented-error
			pyperclip.copy(repr(command))

		elif buttonNameParts[0] == "SHOW":
			functionName = buttonNameParts[2]
			functionService = buttonNameParts[1]
			uniqueTabID = "--".join([functionService, functionName])

			command = self.CallCommandBuilder(functionName, functionService)
			if command is False:
				return

			self.query_one(f"#{uniqueTabID}_commandCollapsible").collapsed = False
			self.query_one(f"#{uniqueTabID}_commandBytes").update(repr(command))

	# Exit when q is pressed (see BINDINGS)
	def action_quit_app(self):
		self.exit()
