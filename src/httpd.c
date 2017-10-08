// HTTP Server - Programming Assignment 2 for Computer Networking
// University of Reykjavík, autumn 2017
// Students: Hreiðar Ólafur Arnarsson, Kristinn Heiðar Freysteinsson & Maciej Sierzputowski
// Usernames: hreidara14, kristinnf13 & maciej15

// Constants:
#define MAX_MESSAGE_LENGTH 1024
#define POLL_SIZE 32
#define TIMEOUT 1000 // TODO: Change to 30000 (30 seconds) before handin

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

// Function declarations
gchar *getCurrentDateTimeAsString();
gchar *getCurrentDateTimeAsISOString();
gchar *getPageString(gchar *host, gchar *reqURL, gchar *clientIP, gchar *clientPort, gchar *data);
void logRecvMessage(gchar *clientIP, gchar *clientPort, gchar *reqMethod, gchar *host, gchar *reqURL, gchar *code);
void sendHeadResponse(int connfd, gchar *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL);
void processGetRequest(int connfd, gchar *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL);
void processPostRequest(int connfd, gchar *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL, gchar *data);
void processNotSupportedRequest(int connfd, gchar *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL);
//void processRequestErrors();
void printHashMap(gpointer key, gpointer value, gpointer user_data);

int main(int argc, char *argv[])
{
	if(argc < 2) {
		g_printf("Format expected is .src/httpd <port_number>\n");
		exit(EXIT_SUCCESS);
	}

    gchar message[MAX_MESSAGE_LENGTH];

    /* Create and bind a TCP socket */
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    /* Network functions need arguments in network byte order instead of
       host byte order. The macros htonl, htons convert the values. */
    struct sockaddr_in server, client;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(atoi(argv[1]));
    bind(sockfd, (struct sockaddr *) &server, (socklen_t) sizeof(server));

    /* Before the server can accept messages, it has to listen to the
       welcome port. A backlog of six connections is allowed. */
    listen(sockfd, 6);

    /* Setting up structs and variables needed for the poll() function */
    struct pollfd pollArray[POLL_SIZE];
    int numOfFds = 1;
    memset(pollArray, 0, sizeof(pollArray));
    pollArray[0].fd = sockfd;
    pollArray[0].events = POLLIN;

    for (;;) {

        // poll() function is used to monitor a set of file descriptors
        int r = poll(pollArray, numOfFds, TIMEOUT);

        if(r == 0) { // This means that the poll() function timed out
            if(numOfFds > 1) {
                for(int i = 1; i < numOfFds; i++) {
                    /* Close the connection. */
                    shutdown(pollArray[i].fd, SHUT_RDWR);
                    close(pollArray[i].fd);
                }
            }
            g_printf("Just checking... The poll() function is timing out.\n");
            continue;
        }

        //Accepting a TCP connection, connfd is a handle dedicated to this connection.
        socklen_t len = (socklen_t) sizeof(client);
        int connfd = accept(sockfd, (struct sockaddr *) &client, &len);

        /* Receive from connfd, not sockfd. */
        ssize_t n = recv(connfd, message, sizeof(message) - 1, 0);
        message[n] = '\0';

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
        // TODO: REMEMBER TO COMMENT OUT OR DELETE BEFORE HANDIN!
        g_hash_table_foreach(hash, (GHFunc)printHashMap, NULL);

        gchar *clientIP   = g_strdup_printf("%s", inet_ntoa(client.sin_addr));
        gchar *clientPort = g_strdup_printf("%i", (int)ntohs(client.sin_port));
        gchar *hostField  = g_strdup_printf("%s", (gchar *)g_hash_table_lookup(hash, "Host:"));
        gchar **hostSplit = g_strsplit_set(hostField, ":", 0);

        if(g_str_has_prefix(message, "HEAD")) {
            sendHeadResponse(connfd, clientIP, clientPort, hostSplit[0], msgSplit[0], msgSplit[1]);
        }
        else if(g_str_has_prefix(message, "GET")) {
            processGetRequest(connfd, clientIP, clientPort, hostSplit[0], msgSplit[0], msgSplit[1]);
        }
        else if(g_str_has_prefix(message, "POST")) {
            processPostRequest(connfd, clientIP, clientPort, hostSplit[0], msgSplit[0], msgSplit[1], msgBody);
        }
        else {
            processNotSupportedRequest(connfd, clientIP, clientPort, hostSplit[0], msgSplit[0], msgSplit[1]);
        }

        g_free(clientIP); g_free(clientPort); g_free(hostField);
        g_strfreev(msgSplit); g_strfreev(hostSplit);
        g_hash_table_destroy(hash);

        /* Close the connection. */
        shutdown(connfd, SHUT_RDWR);
        close(connfd);
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
gchar *getPageString(gchar *host, gchar *reqURL, gchar *clientIP, gchar *clientPort, gchar *data)
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

void logRecvMessage(gchar *clientIP, gchar *clientPort, gchar *reqMethod, gchar *host, gchar *reqURL, gchar *code)
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
        g_printf("Error with logging! File couldn't be opened.");
    }
}

void sendHeadResponse(int connfd, gchar *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL)
{
    gchar *theTime = getCurrentDateTimeAsString();
    gchar *response = g_strconcat("HTTP/1.1 200 OK\r\nDate: ", theTime, "\r\nContent-Type: text/html\r\n",
                                  "Content-length: 0\r\nServer: TheMagicServer/1.0\r\nConnection: Close\r\n\r\n", NULL);
    send(connfd, response, strlen(response), 0);
    logRecvMessage(clientIP, clientPort, reqMethod, host, reqURL, "200");
    g_free(theTime);
    g_free(response);
}

void processGetRequest(int connfd, gchar *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL)
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

void processPostRequest(int connfd, gchar *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL, gchar *data)
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

void processNotSupportedRequest(int connfd, gchar *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL)
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
