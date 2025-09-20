#include "B_tcpServer.h"

static const char* tcpITag = "BarnaNet - TCP Ingress";
static const char* tcpETag = "BarnaNet - TCP Egress";

static struct B_TCPEgressTaskParameter tcpEgressTaskParameter = { 0 };

// Inits the TCP server
// Returns the server socket, but returns 0 if fails
// - Private function
// - !Runs in the TCP task
static int B_InitTCPServer()
{
	int serverSocket = 0;

	struct sockaddr_in dest_addr = {
	    .sin_addr.s_addr = htonl(INADDR_ANY),
	    .sin_family = AF_INET,
	    .sin_port = htons(CONFIG_B_TCP_PORT)
	};

	// Create server socket
	serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (serverSocket < 0) {
		ESP_LOGE(tcpITag, "Unable to create socket: errno %d", errno);
		return 0;
	}

	// Enable reuse address
	int opt = 1;
	setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	ESP_LOGI(tcpITag, "Socket created");

	// Bind socket
	int err = bind(serverSocket, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
	if (err != 0) {
		ESP_LOGE(tcpITag, "Socket unable to bind: errno %d", errno);
		ESP_LOGE(tcpITag, "IPPROTO: %d", AF_INET);
		close(serverSocket);
		return 0;
	}
	ESP_LOGI(tcpITag, "Socket bound to port %d", CONFIG_B_TCP_PORT);

	// Start listening
	err = listen(serverSocket, 1);
	if (err != 0) {
		ESP_LOGE(tcpITag, "Error occurred during listen: errno %d", errno);
		close(serverSocket);
		return 0;
	}

	ESP_LOGI(tcpITag, "Socket listening");
	return serverSocket;
}

// Dispatches the commands sent via TCP
// Doesn't return error, instead fills the responseCommand with an error message and sends it to the egress task
// - Private function
// - !Runs in the TCP task
static void B_HandleTCPMessage(B_addressMap_t* const addressMap, B_command_t* const command, uint8_t transmissionID)
{
	if (addressMap == NULL || command == NULL || transmissionID == 0) {
		ESP_LOGE(tcpITag, "Invalid parameters supplied to B_HandleTCPMessage");
		return;
	}

	// Sanitize request type
	if (B_COMMAND_OP(command->header) == B_COMMAND_OP_RES || B_COMMAND_OP(command->header) == B_COMMAND_OP_ERR) {
		ESP_LOGW(tcpITag, "Invalid request type");

		command->dest = B_TASKID_TCP; // Hack to send it to its egress task and back to the client
		command->transmissionID = transmissionID;
		B_SendStatusReply(addressMap, command, B_TASKID_TCP, B_COMMAND_OP_ERR, "Invalid request type");
		return;
	}

	if (!B_RelayCommand(addressMap, command, B_TASKID_TCP, transmissionID)) {
		ESP_LOGE(tcpITag, "Failed to relay command");

		command->dest = B_TASKID_TCP; // Hack to send it to its egress task and back to the client
		command->transmissionID = transmissionID;
		B_SendStatusReply(addressMap, command, B_TASKID_TCP, B_COMMAND_OP_ERR, "INTERNAL: Relay error");
		return;
	}
}

// Send message to sock
// Arguments: (int sock, const char *const sendBuffer, size_t bufferSize)
// - Private function
// - !Runs in the TCP task
static void B_TCPSendMessage(int sock, const char *const sendBuffer, size_t bufferSize)
{
	if (sock < 0 || sendBuffer == NULL || bufferSize == 0) {
		ESP_LOGE(tcpETag, "Invalid parameters in B_TCPSendMessage");
		return;
	}

	// Send data in chunks if not able to send it in one go
	size_t toWrite = bufferSize;
	while (toWrite > 0) {
		int written = send(sock, sendBuffer + (bufferSize - toWrite), toWrite, 0);
		if (written < 0) {
			ESP_LOGE(tcpETag, "Error occurred during sending: errno %d", errno);
			return;
		}
		toWrite -= written;
	}
}

// Listens for replies to the TCP messages
// - Blocking function
// - !Runs in the TCP Egress task
// - Expected parameter: B_TCPEgressTaskParameter struct
static void B_TCPEgressTask(void* pvParameters)
{
	const struct B_TCPEgressTaskParameter* const taskParameter = (const struct B_TCPEgressTaskParameter* const)pvParameters;
	if (taskParameter == NULL || taskParameter->addressMap == NULL || taskParameter->socketSet == NULL) {
		ESP_LOGE(tcpITag, "The supplied task parameter is invalid, aborting startup");
		vTaskDelete(NULL);
	}

	B_addressMap_t* addressMap = taskParameter->addressMap;
	fd_set* socketSet = taskParameter->socketSet;

	QueueHandle_t tcpQueue = B_GetAddress(addressMap, B_TASKID_TCP);
	if (tcpQueue == B_ADDRESS_MAP_INVALID_QUEUE) {
		ESP_LOGE(tcpETag, "The TCP queue is invalid, aborting startup");
		vTaskDelete(NULL);
	}

	ESP_LOGI(tcpETag, "Started TCP egress task");

	while (true) {
		B_command_t responseCommand = { 0 };
		if (xQueueReceive(tcpQueue, &responseCommand, portMAX_DELAY) != pdTRUE) {
			ESP_LOGE(tcpETag, "Failed to receive command from the TCP queue, discarding");
			continue;
		}

		//ESP_LOGI(tcpETag, "Got command from queue: from=%u, dest=%u, header=%u, transmissionID=%u", responseCommand.from, responseCommand.dest, responseCommand.header, responseCommand.transmissionID);
		//ESP_LOG_BUFFER_HEXDUMP(tcpETag, &responseCommand, sizeof(responseCommand), ESP_LOG_INFO);

		// Check if the socket is still alive
		// The default socket (transmissionID) is 0, but the server or any socket is unlikely to be 0 (mostly 56 or higher)
		if (!FD_ISSET(responseCommand.transmissionID, socketSet)) {
			ESP_LOGW(tcpETag, "Socket is no longer alive");
			continue;
		}

		// Send the response back
		B_TCPSendMessage(responseCommand.transmissionID, (const char*)&responseCommand, sizeof(responseCommand));
	}

	// Task paniced
	// Task doesn't own the sockets, so it doesn't need to close them
	vTaskDelete(NULL);
}

// Listens for TCP messages
// - Blocking function
// - !Runs in the TCP Ingress task
// - Expected parameter: B_TCPIngressTaskParameter struct
void B_TCPIngressTask(void* pvParameters)
{
	const struct B_TCPIngressTaskParameter* const taskParameter = (const struct B_TCPIngressTaskParameter* const)pvParameters;
	if (taskParameter == NULL || taskParameter->addressMap == NULL) {
		ESP_LOGE(tcpITag, "The supplied task parameter is invalid, aborting startup");
		vTaskDelete(NULL);
	}

	int serverSocket = B_InitTCPServer();
	if (serverSocket == 0) {
		ESP_LOGE(tcpITag, "TCP server startup failed, aborting task");
		close(serverSocket);
		vTaskDelete(NULL);
	}

	// Set up the connection set
	fd_set socketSet = { 0 };
	FD_ZERO(&socketSet); // Clear the set
	FD_SET(serverSocket, &socketSet); // Add tcpSocket to set

	// Start the egress task
	tcpEgressTaskParameter.addressMap = taskParameter->addressMap;
	tcpEgressTaskParameter.socketSet = &socketSet;
	xTaskCreate(B_TCPEgressTask, "B_TCPEgressTask", 1024 * 3, &tcpEgressTaskParameter, 3, NULL);

	// Set up tcp keepalive option
	char addr_str[128];
	int keepAlive = LWIP_TCP_KEEPALIVE;
	int keepIdle = 5;
	int keepInterval = 5;
	int keepCount = 3;

	// Listening loop
	while (true) {
		// Create a copy of the socketSet, because select is destructive
		fd_set readableSocketSet = { 0 };
		FD_ZERO(&readableSocketSet);
		FD_COPY(&socketSet, &readableSocketSet);

		// Select all socket with readable data
		// Could use the poll API instead of select, but the ESP-IDF documentation states that poll calls select internally
		// https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/lwip.html#supported-functions
		int readableSocketCount = select(FD_SETSIZE, &readableSocketSet, NULL, NULL, NULL);
		int readSocketCount = 0;

		// fd_set is not an array of file descriptors (like in the Win implementation), but only a mask
		// so I check all the places to find which fd has news
		// Could store all the live fds in an array and only check those
		for (size_t nthSock = 0; nthSock < FD_SETSIZE; nthSock++) {

			// Continue if socket is not set as readable
			if (!FD_ISSET(nthSock, &readableSocketSet))
				continue;

			// Break if all the readable sockets have been read
			if (readSocketCount == readableSocketCount)
				break;

			readSocketCount++;

			// New connection
			if (nthSock == serverSocket) {
				struct sockaddr_storage clientAddress; // Large enough for both IPv4 or IPv6
				socklen_t clientAddrlen = sizeof(clientAddress);

				int newSock = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientAddrlen);
				if (newSock < 0) {
					ESP_LOGE(tcpITag, "Unable to accept new connection: errno %d", errno);
					continue;
				}

				FD_SET(newSock, &socketSet);

				// Set tcp keepalive option
				setsockopt(newSock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
				setsockopt(newSock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
				setsockopt(newSock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
				setsockopt(newSock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

				// Convert ip address to string
				if (clientAddress.ss_family == PF_INET) {
					inet_ntoa_r(((struct sockaddr_in*)&clientAddress)->sin_addr, addr_str, sizeof(addr_str) - 1);
				}

				if (clientAddress.ss_family == PF_INET6) {
					inet6_ntoa_r(((struct sockaddr_in6*)&clientAddress)->sin6_addr, addr_str, sizeof(addr_str) - 1);
				}

				ESP_LOGI(tcpITag, "New connection #%i: (%s)", newSock, addr_str);
				continue;
			}

			// New message
			char receiveBuffer[sizeof(B_command_t)] = { 0 };
			int messageLen = recv(nthSock, (void*)&receiveBuffer, sizeof(B_command_t), 0);
			//ESP_LOGI(tcpITag, "Received %d bytes from socket #%i", messageLen, nthSock);

			// Client error
			if (messageLen < 0) {
				closesocket(nthSock);
				FD_CLR(nthSock, &socketSet);
				ESP_LOGE(tcpITag, "Error occurred during receiving from #%i: errno %d", nthSock, errno);
				continue;
			}

			// Client disconnected
			if (messageLen == 0) {
				closesocket(nthSock);
				FD_CLR(nthSock, &socketSet);
				ESP_LOGI(tcpITag, "Connection closed #%i", nthSock);
				continue;
			}
			
			// Dispatch message handling
			B_HandleTCPMessage(taskParameter->addressMap, (B_command_t* const)&receiveBuffer, nthSock);
		}
	}

	// Task paniced, jump out the nearest window
	for (size_t sock = 0; sock < FD_SETSIZE; sock++) {

		// Continue if socket is not in the set
		if (!FD_ISSET(sock, &socketSet))
			continue;

		FD_CLR(sock, &socketSet);
		close(sock);
	}

	tcpEgressTaskParameter.socketSet = NULL;

	vTaskDelete(NULL);
}
