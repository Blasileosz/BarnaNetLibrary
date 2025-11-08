import library
import random

def empty_command() -> library.Command:
	"""Return a fresh empty command (all zeros)."""
	return library.Command()

def random_command() -> library.Command:
	randomBytes = bytes(random.getrandbits(8) for _ in range(library.B_COMMAND_STRUCT_SIZE))
	command = library.Command(randomBytes)
	return command

print(random_command())
