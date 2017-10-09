// HTTP Server - Programming Assignment 2 for Computer Networking
// University of Reykjavík, autumn 2017
// Students: Hreiðar Ólafur Arnarsson, Kristinn Heiðar Freysteinsson & Maciej Sierzputowski
// Usernames: hreidara14, kristinnf13 & maciej15

// Constants:
#define MAX_MESSAGE_LENGTH 1024
#define MAX_CONNECTIONS 32
#define TIMEOUT 6000 // TODO: Change to 30000 (30 seconds) before handin

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <errno.h>

// A struct to keep info about the connected clients
typedef struct {
	char *ip;
	int port;
} ClientInfo;

// Function declarations
gchar *getCurrentDateTimeAsString();
gchar *getCurrentDateTimeAsISOString();
gchar *getPageString(gchar *host, gchar *reqURL, char *clientIP, gchar *clientPort, gchar *data);
void logRecvMessage(char *clientIP, gchar *clientPort, gchar *reqMethod, gchar *host, gchar *reqURL, gchar *code);
void sendHeadResponse(int connfd, char *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL);
void processGetRequest(int connfd, char *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL);
void processPostRequest(int connfd, char *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL, gchar *data);
void sendNotImplementedResponce(int connfd, char *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL);
//void processRequestErrors();
void printHashMap(gpointer key, gpointer value, gpointer user_data);

int main(int argc, char *argv[])
{
	if(argc < 2) {
		g_printf("Format expected is .src/httpd <port_number>\n");
		exit(EXIT_SUCCESS);
	}

	gchar message[MAX_MESSAGE_LENGTH];
	memset(message, 0, sizeof(message));

	/* Create and bind a TCP socket */
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	/* Network functions need arguments in network byte order instead of
	   host byte order. The macros htonl, htons convert the values. */
	struct sockaddr_in server, client;
	socklen_t len = (socklen_t) sizeof(client);
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(atoi(argv[1]));
	bind(sockfd, (struct sockaddr *) &server, (socklen_t) sizeof(server));

	/* Before the server can accept messages, it has to listen to the
	   welcome port. A backlog of six connections is allowed. */
	listen(sockfd, 6);

	// Setting up structs and variables needed for the poll() function
	struct pollfd pollArray[MAX_CONNECTIONS];
	int numOfFds = 1;
	memset(pollArray, 0, sizeof(pollArray));
	pollArray[0].fd = sockfd;
	pollArray[0].events = POLLIN;

	// Setting up structs to keep info about connected clients
	ClientInfo clientArray[MAX_CONNECTIONS]; // This is of size MAX_CONNECTIONS for simplicity
	memset(clientArray, 0, sizeof(clientArray));

	for (;;) {

		// poll() function is used to monitor a set of file descriptors
		int r = poll(pollArray, numOfFds, TIMEOUT);

		if (r < 0) { // This means that the poll() function has encountered an error
			// If the errno is EINTR (interrupt/signal recieved) we go to the start of the for loop again.
			if (errno == EINTR) continue;
			// Not interrupted and nothing we can do. Break for loop and exit the program.
			perror("poll()");
			break;
		}
		if(r == 0) { // This means that the poll() function timed out
			if(numOfFds > 1) {
				g_printf("The poll() function is timing out and there's at least 1 persistent connection.\n");
				for(int i = 1; i < numOfFds; i++) { /* Close all persistent connections. */
					shutdown(pollArray[i].fd, SHUT_RDWR);
					close(pollArray[i].fd);
					// Not sure if this is needed. Better safe than sorry...
					memset(&pollArray[i], 0, sizeof(struct pollfd));
					memset(&clientArray[i], 0, sizeof(ClientInfo));
				}
				numOfFds = 1;
			}
			continue;
		}

		// We've got past the errors and timeouts. So it's either a new
		// connection or activity on one of the open connections.
		for(int i = 0; i < numOfFds; i++) {
			// Were there any events for this socket?
			if(!pollArray[i].revents) continue;
			// Is there activity on our listening socket?
			if (!i) { 
				// We check if it's a new connection and if we can accept more connections
				if (pollArray[0].revents & POLLIN && numOfFds < MAX_CONNECTIONS) {
					// Accepting a TCP connection, pollArray[x].fd is a handle dedicated to this connection.
					pollArray[numOfFds].fd     = accept(sockfd, (struct sockaddr *) &client, &len);
					pollArray[numOfFds].events = POLLIN;
					clientArray[numOfFds].ip   = inet_ntoa(client.sin_addr);
					clientArray[numOfFds++].port = (int)ntohs(client.sin_port);
					g_printf("Accepted connection on FD nr. \"%d\"\n", pollArray[numOfFds-1].fd);
				}
				else {
					// Either an error occurred or the maximum number of connections has been reached.
					// We might have to handle that but not sure how...
				}
				continue;
			}
			// Is there incoming data on the socket?
			if (pollArray[i].revents & POLLIN) {
				ssize_t n = recv(pollArray[i].fd, message, sizeof(message) - 1, 0);
				message[n] = '\0';

				// This is used for initial connection tests and debugging
				// Prints out the recieved message as a line of hex characters
				/*for(unsigned int i = 0; i < n; i++) g_printf("%hhx ", message[i]);
				g_printf("\n");*/

				gchar *msgBody = g_strrstr(message, "\r\n\r\n\0");
				msgBody[0] = 0; msgBody += 4;
				gchar **msgSplit = g_strsplit_set(message, " \r\n", 0);

				GHashTable *hash = g_hash_table_new(g_str_hash, g_str_equal);
				/* We deduct 1 from g_strv_length because we count from 0 */
				/* And we add 3 each time because there're empty strings in between each pair */
				for(unsigned int i = 4; i < g_strv_length(msgSplit) - 1; i = i + 3) {
					g_hash_table_insert(hash, msgSplit[i], msgSplit[i+1]);
				}

				// For debugging. Iterates through the hash map and prints out each key-value pair
				// TODO: REMEMBER TO COMMENT OUT (OR DELETE) BEFORE HANDIN!
				//g_hash_table_foreach(hash, (GHFunc)printHashMap, NULL);

				gchar *clientPort = g_strdup_printf("%i", clientArray[i].port);
				gchar *hostField  = g_strdup_printf("%s", (gchar *)g_hash_table_lookup(hash, "Host:"));
				gchar **hostSplit = g_strsplit_set(hostField, ":", 0);
				gchar *connField  = g_strdup_printf("%s", (gchar *)g_hash_table_lookup(hash, "Connection:"));
				int persistent    = 0;
				if(g_str_has_prefix(connField, "keep-alive")) {
					persistent = 1;
				}

				//g_printf("CHECK! CHECK! CHECK! - Nr. 1\n");

				if(g_str_has_prefix(message, "HEAD")) {
					sendHeadResponse(pollArray[i].fd, clientArray[i].ip, clientPort, hostSplit[0], msgSplit[0], msgSplit[1]);
				}
				else if(g_str_has_prefix(message, "GET")) {
					processGetRequest(pollArray[i].fd, clientArray[i].ip, clientPort, hostSplit[0], msgSplit[0], msgSplit[1]);
				}
				else if(g_str_has_prefix(message, "POST")) {
					processPostRequest(pollArray[i].fd, clientArray[i].ip, clientPort, hostSplit[0], msgSplit[0], msgSplit[1], msgBody);
				}
				else {
					sendNotImplementedResponce(pollArray[i].fd, clientArray[i].ip, clientPort, hostSplit[0], msgSplit[0], msgSplit[1]);
				}

				g_free(clientPort); g_free(hostField);
				g_strfreev(msgSplit); g_strfreev(hostSplit);
				g_hash_table_destroy(hash);
				memset(message, 0, sizeof(message));

				// If it's not a persistent connection, we close it right here
				if(!persistent) {
					shutdown(pollArray[i].fd, SHUT_RDWR);
					close(pollArray[i].fd);
					pollArray[i].fd = -1;
					// TODO: Add functionality that removes inactive FDs!
				} // else it gets closed when there's a timeout
			}
		}
	}
}

gchar *getCurrentDateTimeAsString()
{
	GDateTime *currTime  = g_date_time_new_now_local();
	gchar *dateAsString = g_date_time_format(currTime, "%a, %d %b %Y %H:%M:%S %Z");
	g_date_time_unref(currTime);
	return dateAsString;
}

gchar *getCurrentDateTimeAsISOString()
{
	GTimeVal theTime;
	g_get_current_time(&theTime);
	return g_time_val_to_iso8601(&theTime);
}

/* creates the page needed for GET and POST methods. Data is NULL for a GET request. */
gchar *getPageString(gchar *host, gchar *reqURL, char *clientIP, gchar *clientPort, gchar *data)
{
	gchar *page;
	gchar *firstPart = g_strconcat("<!DOCTYPE html>\n<html>\n<head>\n</head>\n<body>\n<p>http://", host, reqURL,
								   " ", clientIP, ":", clientPort, "</p>\n", NULL);
	gchar *lastPart = g_strconcat("</body>\n</html>\n", NULL);
	if(data != NULL) {
		page = g_strconcat(firstPart, data, "\n", lastPart, NULL);
	} else {
		page = g_strconcat(firstPart, lastPart, NULL);
	}
	g_free(firstPart); g_free(lastPart);
	return page;
}

void logRecvMessage(char *clientIP, gchar *clientPort, gchar *reqMethod, gchar *host, gchar *reqURL, gchar *code)
{
	FILE *fp;
	fp = fopen("log.txt", "a");
	if(fp != NULL) {
		gchar *theTime = getCurrentDateTimeAsISOString();
		gchar *logMsg = g_strconcat(theTime, " : ", clientIP, ":", clientPort, " ", reqMethod, " ", host, reqURL, " : ", code, "\n", NULL);
		fwrite(logMsg, sizeof(char), strlen(logMsg), fp);
		g_free(logMsg);
		g_free(theTime);
		fclose(fp);
	} else {
		perror("fopen()");
	}
}

void sendHeadResponse(int connfd, char *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL)
{
	gchar *theTime = getCurrentDateTimeAsString();
	gchar *response = g_strconcat("HTTP/1.1 200 OK\r\nDate: ", theTime, "\r\nContent-Type: text/html\r\n",
								  "Content-length: 0\r\nServer: TheMagicServer/1.0\r\nConnection: Close\r\n\r\n", NULL);
	send(connfd, response, strlen(response), 0);
	logRecvMessage(clientIP, clientPort, reqMethod, host, reqURL, "200");
	g_free(theTime);
	g_free(response);
}

void processGetRequest(int connfd, char *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL)
{
	gchar *theTime = getCurrentDateTimeAsString();
	gchar *page = getPageString(host, reqURL, clientIP, clientPort,  NULL);
	gchar *contLength = g_strdup_printf("%i", (int)strlen(page));
	gchar *response = g_strconcat("HTTP/1.1 200 OK\r\nDate: ", theTime, "\r\nContent-Type: text/html\r\nContent-length: ",
								  contLength, "\r\nServer: TheMagicServer/1.0\r\n\r\n", page, NULL);
	send(connfd, response, strlen(response), 0);
	logRecvMessage(clientIP, clientPort, reqMethod, host, reqURL, "200");
	g_free(theTime);
	g_free(page);
	g_free(response);
}

void processPostRequest(int connfd, char *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL, gchar *data)
{
	gchar *theTime = getCurrentDateTimeAsString();
	gchar *page = getPageString(host, reqURL, clientIP, clientPort, data);
	gchar *contLength = g_strdup_printf("%i", (int)strlen(page));
	gchar *response = g_strconcat("HTTP/1.1 201 OK\r\nDate: ", theTime, "\r\nContent-Type: text/html\r\nContent-length: ",
								  contLength, "\r\nServer: TheMagicServer/1.0\r\n\r\n", page, NULL);
	send(connfd, response, strlen(response), 0);
	logRecvMessage(clientIP, clientPort, reqMethod, host, reqURL, "201");
	g_free(theTime);
	g_free(page);
	g_free(response);
}

void sendNotImplementedResponce(int connfd, char *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL)
{
	gchar *theTime = getCurrentDateTimeAsString();
	gchar *response = g_strconcat("HTTP/1.1 501 Not Implemented\r\nDate: ", theTime, "\r\nContent-Type: text/html\r\n",
								  "Content-length: 0\r\nServer: TheMagicServer/1.0\r\n\r\n", NULL);
	send(connfd, response, strlen(response), 0);
	logRecvMessage(clientIP, clientPort, reqMethod, host, reqURL, "501");
	g_free(theTime);
	g_free(response);
}

/**
 * This function handles method request errors and sends an error message to the client
 * Method request errors means all requests that are not of the type GET, HEAD or POST
 * @param
 * @returns
 */
/*void processRequestErrors()
{
	
}*/

// This function is used to help with debugging. It's passed as a GHFunc to the
// g_hash_table_foreach function and prints out each (key, value) in a hash map
void printHashMap(gpointer key, gpointer value, gpointer user_data) {
	user_data = user_data; // To get rid of the unused param warning
	g_printf("The key is \"%s\" and the value is \"%s\"\n", (char *)key, (char *)value);
}
