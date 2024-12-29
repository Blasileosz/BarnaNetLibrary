#include "B_tcpServer.h"

static const char *tcpTag = "BarnaNet - TCP";

// Inits the TCP server
// - Private function
// - !Runs in the main task
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
	ESP_LOGI(tcpTag, "Socket bound %d", CONFIG_B_TCP_PORT);

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

// Send message to sock
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

void B_TCPTask(void* pvParameters)
{
	// Get the callback function from the task parameter
	void (*handlerFunctionPointer)(const B_command_t* const, B_command_t* const) = pvParameters;
	if (handlerFunctionPointer == NULL) {
		ESP_LOGE(tcpTag, "The handler function pointer passed is invalid, aborting startup");
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
					ESP_LOGE(tcpTag, "Unable to accept connection: errno %d", errno);
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

				ESP_LOGI(tcpTag, "Socket (#%i) accepted ip address: %s", newSock, addr_str);
				continue;
			}

			// New message
			B_command_t messageBuffer = { 0 };
			int messageLen = recv(nthSock, (void*)&messageBuffer, sizeof(B_command_t), 0);

			ESP_LOGI(tcpTag, "Received %d bytes from socket #%i", messageLen, nthSock);

			// Client error
			if (messageLen < 0) {
				closesocket(nthSock);
				FD_CLR(nthSock, &socketSet);
				ESP_LOGE(tcpTag, "Error occurred during receiving: errno %d", errno);
				continue;
			}

			// Client disconnected
			if (messageLen == 0) {
				closesocket(nthSock);
				FD_CLR(nthSock, &socketSet);
				ESP_LOGI(tcpTag, "Connection closed");
				continue;
			}

			// Dispatch message handling
			B_command_t responseBuffer = { 0 };
			handlerFunctionPointer((const B_command_t* const)&messageBuffer, &responseBuffer);

			// TODO: If no response was written, nothing should be sent back?
			int responseLen = sizeof(B_command_t);
			if (responseLen > 0) {
				B_TCPSendMessage(nthSock, (const char* const)&responseBuffer, responseLen);
				continue;
			}
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
