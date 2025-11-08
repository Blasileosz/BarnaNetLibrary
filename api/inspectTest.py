import inspect
import library
import AlarmService
from typing import TypeVar, Type

def testClassInspect():
	members = inspect.getmembers(alarms.AlarmTask, predicate=inspect.isfunction)
	getFunctions = filter(lambda member: member[0].startswith("Get"), members)

	for (functionName, definition) in getFunctions:
		# instatiate method (if only the name was available)
		function = getattr(alarms.AlarmTask, functionName)
		print(function == definition)

		description = inspect.getdoc(definition)
		arguments = inspect.signature(definition)

		print(f"Function: {functionName}\n--description: {description}")
		for argName, argParam in arguments.parameters.items():
			print(f"--arg name: {argName}, param: {argParam.annotation}")

def testFunctionCall(test: Type[library.Service]):
	#classInstance = getattr(alarms, "AlarmTask")
	function = getattr(test, "GetTimepart")
	args = { "hours": 12, "minutes": 12, "seconds": 12 }
	result = function(**args)
	print(result)


def EnumerateServices(services: list[TypeVar('Service')]) -> list[type]:
	service_list = []

	for service in services:
		if not issubclass(service, library.Service):
			continue

		service_list.append(service)
	return service_list

def EnumerateServiceFunctions(service: Type[library.Service]):
	members = inspect.getmembers(service, predicate=inspect.isfunction)
	buildFunctions = filter(lambda member: member[0].startswith("Build_"), members)

	return list(buildFunctions)

def EnumerateFunctionArgs(service: Type[library.Service], functionName: str):
	members = inspect.getmembers(service, predicate=inspect.isfunction)
	function = dict(members).get(functionName, None)

	if function is None:
		print("Function not found")
		return
	
	functionSignature = {}

	description = inspect.getdoc(function)
	functionSignature["description"] = description

	arguments = inspect.signature(function)

	for argName, argParam in arguments.parameters.items():
		functionSignature[argName] = argParam.annotation

	return functionSignature

print(EnumerateServiceFunctions(AlarmService.AlarmService)[0][1](5).to_bytes())
