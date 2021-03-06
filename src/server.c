/**
 * webserver.c -- A webserver written in C
 * 
 * Test with curl (if you don't have it, install it):
 * 
 *    curl -D - http://localhost:3490/
 *    curl -D - http://localhost:3490/d20
 *    curl -D - http://localhost:3490/date
 * 
 * You can also test the above URLs in your browser! They should work!
 * 
 * Posting Data:
 * 
 *    curl -D - -X POST -H 'Content-Type: text/plain' -d 'Hello, sample data!' http://localhost:3490/save
 * 
 * (Posting data is harder to test from a browser.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/file.h>
#include <fcntl.h>
#include "net.h"
#include "file.h"
#include "mime.h"
#include "cache.h"

#define PORT "3490"  // the port users will be connecting to

#define SERVER_FILES "./serverfiles"
#define SERVER_ROOT "./serverroot"

/**
 * Send an HTTP response
 *
 * header:       "HTTP/1.1 404 NOT FOUND" or "HTTP/1.1 200 OK", etc.
 * content_type: "text/plain", etc.
 * body:         the data to send.
 * 
 * Return the value from the send() function.
 */
int send_response(int fd, char *header, char *content_type, void *body, int content_length)
{
    const int max_response_size = 65536;
    char response[max_response_size];

    time_t rawtime;
    struct tm *info;
    time(&rawtime);
    info = localtime(&rawtime);

    // Build HTTP response and store it in response || double \n here because it'll mess up the HTML loading
    // int response_length = sprintf(response, "%s\nDate: %s\nConnection: close\nContent-length: %d\nContent-type: %s\n\n%s\n", header, asctime(info), content_length, content_type, body);
    int response_length = sprintf(response,
        "%s\n"
        "Date: %s"
        "Content-Length: %d\n"
        "Content-Type: %s\n"
        "Connection: close\n"
        "\n"
        "%s\n",
        header,
        asctime(info),
        content_length,
        content_type,
        body
    );
    printf("response: %s\n", response); // --> We can use a sprintf() to create the response, also returns total number of bytes in result string
    // memcpy(response + response_length, body, content_length);  

    // Send it all!
    int rv = send(fd, response, response_length, 0);

    if (rv < 0) {
        perror("send");
    }

    return rv;
}


/**
 * Send a /d20 endpoint response
 */
void get_d20(int fd)
{
    // Generate a random number between 1 and 20 inclusive
    // 1. Set seed for rand()
    srand( time(0) );

    // 2. Make the random number
    int random_number = ( rand() % 20 );
    printf("random number is: %d\n", random_number);

    // 3. Assign the number to the data given into send_response 3rd arg
    char data[50];
    sprintf(data, "%d", random_number);

    // 4. Use send_response() to send it back as text/plain data || 
    /*
    Argument List for Send_response():
        1. File Descriptor (this is passed in as an argument)
        2. This is just a mime of the request_type, set it yourself
        3. Block of "data" --> we assigned the rand number into it
        4. Size of the data
    */
    send_response(fd, "HTTP/1.1 200 OK, MIME", "text/plain", data, strlen(data));
    ///////////////////
    // IMPLEMENT ME! //
    ///////////////////
}

/**
 * Send a 404 response
 */
void resp_404(int fd)
{
    char filepath[4096];
    struct file_data *filedata; 
    char *mime_type;

    // Fetch the 404.html file
    snprintf(filepath, sizeof filepath, "%s/404.html", SERVER_FILES);
    filedata = file_load(filepath);

    if (filedata == NULL) {
        // TODO: make this non-fatal
        fprintf(stderr, "cannot find system 404 file\n");
        exit(3);
    }

    mime_type = mime_type_get(filepath);

    send_response(fd, "HTTP/1.1 404 NOT FOUND", mime_type, filedata->data, filedata->size);

    file_free(filedata);
}

/**
 * Read and return a file from disk or cache
 */
void get_file(int fd, struct cache *cache, char *request_path)
{
// =========================WEB SERVER II===============================
    struct cache_entry *find_entry = cache_get(cache, request_path);
    if (find_entry != NULL) {
        printf("Get cached :D\n");
        send_response(
            fd,
            "HTTP/1.1 200 OK",
            find_entry->content_type,
            find_entry->content,
            find_entry->content_length
        );
        return;
    }


// =====================================================================
    // 1. Create a file_path buffer & initialize a file_data struct
    char file_path[4096];
    struct file_data *filedata; 
    char *mime_type;  

    // 2. Handle whether or not the request path is empty (aka the home page) or snprintf() the another file path
    if ( strcmp(request_path, "/") == 0 ) {        
        snprintf(file_path, sizeof file_path, "%s%s", SERVER_ROOT, "/index.html");
    } else {
        snprintf(file_path, sizeof file_path, "%s%s", SERVER_ROOT, request_path);
    }
    // 3. Load file data
    filedata = file_load(file_path);

    // 4. Check if file_load() encountered an error, send response to server and free file
    if (filedata == NULL) {
        resp_404(fd);
    } else {
        mime_type = mime_type_get(file_path);
        send_response(fd, "HTTP/1.1 200 OK", mime_type, filedata->data, filedata->size);
        cache_put(cache, file_path, mime_type, filedata->data, filedata->size);
        file_free(filedata);
    }
}

/**
 * Search for the end of the HTTP header
 * "Newlines" in HTTP can be \r\n (carriage return followed by newline) or \n
 * (newline) or \r (carriage return).
 */
char *find_start_of_body(char *header)
{
    ///////////////////
    // IMPLEMENT ME! // (Stretch)
    ///////////////////
}

/**
 * Handle HTTP request and send response
 */
void handle_http_request(int fd, struct cache *cache)
{
    const int request_buffer_size = 65536; // 64K
    char request[request_buffer_size]; // --> Holds entire HTTP request once recv() call returns

    // Read request Args:
    // 1. Socket --> Specifies socket file descriptor
    // 2. Buffer --> points to a buffer where the message should be stored
    // 3. Length --> Length in bytes of the buffer pointed to in 2nd arg
    // 4. Flags -->  Specifies the type of message reception
    int bytes_recvd = recv(fd, request, request_buffer_size - 1, 0);

    if (bytes_recvd < 0) {
        perror("recv");
        return;
    }

    ///////////////////
    // IMPLEMENT ME! //
    ///////////////////

    // Read the three components of the first request line || we can do this with sscanf()

    /*
    - Arg List:
    - 1. Pointer to the string from where we want to read data
    - How to use: We first need to supply three vars of appropriate type

    * header:       "HTTP/1.1 404 NOT FOUND" or "HTTP/1.1 200 OK", etc.
    * content_type: "text/plain", etc.
    * body:         the data to send.
    */    
    char request_type[20], request_endpoint[20], request_http[100];
    sscanf(request, "%s %s %s", request_type, request_endpoint, request_http);
    printf("Inside my handle_http: \n%s\n%s\n%s\n\n\n\n", request_type, request_endpoint, request_http);

    // If GET, handle the get endpoints
    if(!strcmp(request_type, "GET")) {
        // Check if it's /d20 and handle that special case
        // printf("strcmp GET print check\n");
        if (!strcmp(request_endpoint, "/d20")) {
            // printf("Request-endpoint: /d20\n");
            get_d20(fd);
        } else {
            // Otherwise serve the requested file by calling get_file()
            /*
                - int fd, struct cache *cache, char *request_path
            */
            // printf("Serve requested file (get_file)\n");
            get_file(fd, cache, request_endpoint);
        }
    }
    
    // (Stretch) If POST, handle the post request
}

/**
 * Main
 */
int main(void)
{
    int newfd;  // listen on sock_fd, new connection on newfd
    struct sockaddr_storage their_addr; // connector's address information
    char s[INET6_ADDRSTRLEN];

    struct cache *cache = cache_create(10, 0);

    // Get a listening socket
    int listenfd = get_listener_socket(PORT);

    if (listenfd < 0) {
        fprintf(stderr, "webserver: fatal error getting listening socket\n");
        exit(1);
    }
    // resp_404(listenfd);

    printf("webserver: waiting for connections on port %s...\n", PORT);

    // This is the main loop that accepts incoming connections and
    // forks a handler process to take care of it. The main parent
    // process then goes back to waiting for new connections.
    
    while(1) {        
        socklen_t sin_size = sizeof their_addr;

        // Parent process will block on the accept() call until someone
        // makes a new connection:
        newfd = accept(listenfd, (struct sockaddr *)&their_addr, &sin_size);
        if (newfd == -1) {
            perror("accept");
            continue;
        }

        // Print out a message that we got the connection
        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s);
        printf("server: got connection from %s\n", s);
        
        // newfd is a new socket descriptor for the new connection.
        // listenfd is still listening for new connections.

        handle_http_request(newfd, cache);

        close(newfd);
    }

    // Unreachable code

    return 0;
}