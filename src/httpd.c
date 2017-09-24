// HTTP Server - Programming Assignment 2 for Computer Networking
// University of Reykjavík, autumn 2017
// Students: Hreiðar Ólafur Arnarsson, Kristinn Heiðar Freysteinsson & Maciej Sierzputowski
// Usernames: hreidara14, kristinnf13 & maciej15

// Constants:
#define MAX_MESSAGE_LENGTH 1024

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
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

gchar *getCurrentDateTimeAsString() {
    GDateTime *currTime  = g_date_time_new_now_local();
    gchar *dateAsString = g_date_time_format(currTime, "%a, %d %b %Y %H:%M:%S %Z");
    g_date_time_unref(currTime);
    return dateAsString;
}

gchar *getCurrentDateTimeAsISOString() {
    GTimeVal theTime;
    g_get_current_time(&theTime);
    return g_time_val_to_iso8601(&theTime);
}

/* creates the page needed for GET and POST methods. Data is NULL for a GET request. */
gchar *getPageString(gchar *host, gchar *reqURL, gchar *clientIP, gchar *clientPort, gchar *data) {
    gchar *page;
    gchar *firstPart = g_strconcat("<!DOCTYPE html>\n<html>\n<head>\n</head>\n<body>\n<p>http://", host, reqURL,
                                   " ", clientIP, ":", clientPort, "</p>\n", NULL);
    gchar *lastPart = g_strconcat("</body>\n", NULL);
    if(data != NULL) {
        page = g_strconcat(firstPart, data, "\n", lastPart, NULL);
    } else {
        page = g_strconcat(firstPart, lastPart, NULL);
    }
    g_free(firstPart); g_free(lastPart);
    return page;
}

void logRecvMessage(gchar *clientIP, gchar *clientPort, gchar *reqMethod, gchar *host, gchar *reqURL, gchar *code) {
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

void sendHeadResponse(int connfd, gchar *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL) {
    gchar *theTime = getCurrentDateTimeAsString();
    gchar *response = g_strconcat("HTTP/1.1 200 OK\r\nDate: ", theTime, "\r\nContent-Type: text/html\r\n",
                                  "Content-length: 0\r\nServer: StzHttp/1.0\r\nConnection: Close\r\n\r\n", NULL);
    send(connfd, response, strlen(response), 0);
    logRecvMessage(clientIP, clientPort, reqMethod, host, reqURL, "200");
    g_free(theTime);
    g_free(response);
}

void processGetRequest(int connfd, gchar *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL) {
    gchar *theTime = getCurrentDateTimeAsString();
    gchar *page = getPageString(host, reqURL, clientIP, clientPort,  NULL);
    gchar *contLength = g_strdup_printf("%i", (int)strlen(page));
    gchar *response = g_strconcat("HTTP/1.1 200 OK\r\nDate: ", theTime, "\r\nContent-Type: text/html\r\nContent-length: ",
                                  contLength, "\r\nServer: StzHttp/1.0\r\nConnection: keep-alive\r\n\r\n", page, NULL);
    send(connfd, response, strlen(response), 0);
    logRecvMessage(clientIP, clientPort, reqMethod, host, reqURL, "200");
    g_free(theTime);
    g_free(page);
    g_free(response);
}

void processPostRequest(int connfd, gchar *clientIP, gchar *clientPort, gchar *host, gchar *reqMethod, gchar *reqURL, gchar *data) {
    gchar *theTime = getCurrentDateTimeAsString();
    gchar *page = getPageString(host, reqURL, clientIP, clientPort, data);
    gchar *contLength = g_strdup_printf("%i", (int)strlen(page));
    gchar *response = g_strconcat("HTTP/1.1 200 OK\r\nDate: ", theTime, "\r\nContent-Type: text/html\r\nContent-length: ",
                                  contLength, "\r\nServer: StzHttp/1.0\r\nConnection: keep-alive\r\n\r\n", page, NULL);
    send(connfd, response, strlen(response), 0);
    logRecvMessage(clientIP, clientPort, reqMethod, host, reqURL, "200");
    g_free(theTime);
    g_free(page);
    g_free(response);
}

int main(int argc, char *argv[])
{
    int sockfd;
    struct sockaddr_in server, client;

	if(argc < 2) {
		g_printf("Format expected is .src/httpd <port_number>\n");
		exit(EXIT_SUCCESS);
	}

    gchar message[MAX_MESSAGE_LENGTH];

    /* Create and bind a TCP socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    /* Network functions need arguments in network byte order instead of
       host byte order. The macros htonl, htons convert the values. */
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(atoi(argv[1]));
    bind(sockfd, (struct sockaddr *) &server, (socklen_t) sizeof(server));

    /* Before the server can accept messages, it has to listen to the
       welcome port. A backlog of six connections is allowed. */
    listen(sockfd, 6);

    for (;;) {
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
        /* We deduct 1 from g_strv_length because we cound from 0 */
        /* And we add 3 each time because there're empty strings in between each pair */
        for(unsigned int i = 4; i < g_strv_length(msgSplit) - 1; i = i + 3) {
            g_hash_table_insert(hash, msgSplit[i], msgSplit[i+1]);
        }

        gchar *clientIP = g_strdup_printf("%s", inet_ntoa(client.sin_addr));
        gchar *clientPort = g_strdup_printf("%i", (int)ntohs(client.sin_port));
        gchar *hostField = g_strdup_printf("%s", (gchar *)g_hash_table_lookup(hash, "Host:"));
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
            g_printf("Request not supported.\n");
        }

        g_free(clientIP); g_free(clientPort); g_free(hostField);
        g_strfreev(msgSplit); g_strfreev(hostSplit);
        g_hash_table_destroy(hash);

        /* Close the connection. */
        shutdown(connfd, SHUT_RDWR);
        close(connfd);
    }
}