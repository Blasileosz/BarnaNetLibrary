#include "B_tcpServer.h"

static const char* tcpTag = "BarnaNet - TCP";

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
		ESP_LOGE(tcpTag, "Unable to create socket: errno %d", errno);
		return 0;
	}

	// Enable reuse address
	int opt = 1;
	setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	ESP_LOGI(tcpTag, "Socket created");

	// Bind socket
	int err = bind(serverSocket, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
	if (err != 0) {
		ESP_LOGE(tcpTag, "Socket unable to bind: errno %d", errno);
		ESP_LOGE(tcpTag, "IPPROTO: %d", AF_INET);
		close(serverSocket);
		return 0;
	}
	ESP_LOGI(tcpTag, "Socket bound to port %d", CONFIG_B_TCP_PORT);

	// Start listening
	err = listen(serverSocket, 1);
	if (err != 0) {
		ESP_LOGE(tcpTag, "Error occurred during listen: errno %d", errno);
		close(serverSocket);
		return 0;
	}

	ESP_LOGI(tcpTag, "Socket listening");
	return serverSocket;
}

// Dispatches the commands sent via TCP
// - Private function
// - !Runs in the TCP task
static void B_HandleTCPMessage(const B_command_t* const command, B_command_t* const responseCommand, B_addressMap_t* addressMap)
{
	// Sanitize request type
	if (B_COMMAND_OP(command->header) == B_COMMAND_OP_RES || B_COMMAND_OP(command->header) == B_COMMAND_OP_ERR) {
		ESP_LOGW(tcpTag, "Invalid request type");
		B_FillCommandHeader(responseCommand, B_TASKID_TCP, command->dest, B_COMMAND_OP_ERR, B_COMMAND_ID(command->header));
		B_FillCommandBodyString(responseCommand, "Invalid request type");
		return;
	}

	// Select task to relay command
	QueueHandle_t destAddress = B_GetAddress(addressMap, command->dest);
	if (destAddress == NULL || command->dest == B_TASKID_TCP) {
		ESP_LOGW(tcpTag, "Invalid DEST");
		B_FillCommandHeader(responseCommand, B_TASKID_TCP, command->dest, B_COMMAND_OP_ERR, B_COMMAND_ID(command->header));
		B_FillCommandBodyString(responseCommand, "Invalid DEST");
		return;
	}

	// Relay the command to the selected task
	if (xQueueSend(destAddress, (void* const)command, 0) != pdPASS) {
		ESP_LOGE(tcpTag, "Command unable to be inserted into the queue");
		B_FillCommandHeader(responseCommand, B_TASKID_TCP, command->dest, B_COMMAND_OP_ERR, B_COMMAND_ID(command->header));
		B_FillCommandBodyString(responseCommand, "INTERNAL: Relay error");
		return;
	}

	// Always wait for reply
	QueueHandle_t tcpQueue = B_GetAddress(addressMap, B_TASKID_TCP);
	if (xQueueReceive(tcpQueue, (void* const)responseCommand, pdMS_TO_TICKS(B_TCP_REPLY_TIMEOUT)) != pdPASS) {
		ESP_LOGE(tcpTag, "Recipient did not reply to the command");
		B_FillCommandHeader(responseCommand, B_TASKID_TCP, command->dest, B_COMMAND_OP_ERR, B_COMMAND_ID(command->header));
		B_FillCommandBodyString(responseCommand, "Recipient did not reply to the command");
	}
}

// Send message to sock
// Arguments: (int sock, const char *const sendBuffer, size_t bufferSize)
// - Private function
// - !Runs in the TCP task
static void B_TCPSendMessage(int sock, const char *const sendBuffer, size_t bufferSize)
{
	// Send data in chunks if not able to send it in one go
	size_t toWrite = bufferSize;
	while (toWrite > 0) {
		int written = send(sock, sendBuffer + (bufferSize - toWrite), toWrite, 0);
		if (written < 0) {
			ESP_LOGE(tcpTag, "Error occurred during sending: errno %d", errno);
			return;
		}
		toWrite -= written;
	}
}

// Listens for TCP messages
// - Blocking function
// - !Runs in the TCP task
// - Expected parameter: B_TcpTaskParameter struct
void B_TCPTask(void* pvParameters)
{
	const struct B_TcpTaskParameter* const taskParameter = (const struct B_TcpTaskParameter* const)pvParameters;
	if (taskParameter == NULL || taskParameter->addressMap == NULL) {
		ESP_LOGE(tcpTag, "The supplied task parameter is invalid, aborting startup");
		vTaskDelete(NULL);
	}

	int serverSocket = B_InitTCPServer();
	if (serverSocket == 0) {
		ESP_LOGE(tcpTag, "TCP server startup failed, aborting task");
		close(serverSocket);
		vTaskDelete(NULL);
	}

	struct fd_set socketSet = { 0 };
	FD_ZERO(&socketSet); // Clear the set
	FD_SET(serverSocket, &socketSet); // Add tcpSocket to set

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
					ESP_LOGE(tcpTag, "Unable to accept new connection: errno %d", errno);
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

				ESP_LOGI(tcpTag, "New connection #%i: (%s)", newSock, addr_str);
				continue;
			}

			// New message
			B_command_t messageBuffer = { 0 };
			int messageLen = recv(nthSock, (void*)&messageBuffer, sizeof(B_command_t), 0);
			//ESP_LOGI(tcpTag, "Received %d bytes from socket #%i", messageLen, nthSock);

			// Client error
			if (messageLen < 0) {
				closesocket(nthSock);
				FD_CLR(nthSock, &socketSet);
				ESP_LOGE(tcpTag, "Error occurred during receiving from #%i: errno %d", nthSock, errno);
				continue;
			}

			// Client disconnected
			if (messageLen == 0) {
				closesocket(nthSock);
				FD_CLR(nthSock, &socketSet);
				ESP_LOGI(tcpTag, "Connection closed #%i", nthSock);
				continue;
			}

			// Fill the receive command's from field
			messageBuffer.from = B_TASKID_TCP;

			// Create response command buffer and initialize it to a generic OK response
			B_command_t responseBuffer = { 0 };
			
			// Dispatch message handling
			B_HandleTCPMessage((const B_command_t* const)&messageBuffer, &responseBuffer, taskParameter->addressMap);

			// Respond every time
			B_TCPSendMessage(nthSock, (const char* const)&responseBuffer, sizeof(B_command_t));
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
	vTaskDelete(NULL);
}
