#if 0
TODO:
    -websockets.h
        -alert client websocket thread about closure from background thread
#endif

#include "ns_common.h"
#include "ns_message_queue.h"
#include "ns_socket_wrapper.h"
#include "ns_websockets.h"
#include "ns_files.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <semaphore.h>
#include <time.h>


#define WEBSERVER_PORT "3490"
#define VIDEO_STREAM_PORT "3491"


// TODO: sync this
pthread_t thread_pool[20];
int thread_pool_size;


void *websocket_client_thread_entry(void *thread_data)
{
    NsWebSocket *websocket = (NsWebSocket *)thread_data;

#if 1
    // perform basic test
    {
        char test_message[256];
        int message_length = ns_websocket_receive(websocket, test_message, sizeof(test_message));
        if(message_length == NS_ERROR)
        {
            exit(1);
        }

        printf("test message: %s\n", test_message);

        if(strcmp(test_message, "this is a test"))
        {
            DebugPrintInfo();
            exit(1);
        }

        strcat(test_message, " - received!");
        if(ns_websocket_send(websocket, test_message, strlen(test_message)) == NS_ERROR)
        {
            exit(1);
        }
    }

    printf("basic test passed\n");
#endif

#if 1
    while(1)
    {
#if 0
        uint8_t message[256];
        int message_length = recv(socket, message);
        if(message_length == -1)
        {
            exit(1);
        }
        message[message_length] = 0;

        printf("socket message received (%d bytes): %s\n", strlen((char *)message), (char *)message);

        strcat((char *)message, " - received!");
        if(!send(socket, message, strlen((char *)message)))
        {
            exit(1);
        }
#endif

        char *message = (char *)"funny cat photos";
        if(ns_websocket_send(websocket, message, (strlen(message) + 1)) == NS_ERROR)
        {
            exit(1);
        }

        usleep(500000);
    }
#endif

    return 0;
}

void *stream_thread_entry(void *data)
{
    NsWebSocket websocket;
    if(ns_websocket_create(&websocket, VIDEO_STREAM_PORT) == NS_ERROR)
    {
        DebugPrintInfo();
        exit(1);
    }

    NsWebSocket clients[10];
    int num_clients = 0;
    while(1)
    {
        ns_websocket_get_client(&websocket, &clients[num_clients]);
        pthread_create(&thread_pool[thread_pool_size++], NULL, websocket_client_thread_entry, &clients[num_clients++]);
    }

    return 0;
}

int main(void)
{
    ns_sockets_init();

    pthread_create(&thread_pool[thread_pool_size++], NULL, stream_thread_entry, NULL);

    NsSocket socket;
    if(ns_socket_create(&socket, WEBSERVER_PORT) == NS_ERROR)
    {
        exit(1);
    }

    char webpage[Kilobytes(4)];
    int webpage_size;
    {
        NsFile file;
        ns_file_open(&file, (char *)"webpage.html");

        webpage_size = ns_file_load(&file, webpage, sizeof(webpage));
        if(webpage_size == NS_ERROR)
        {
            exit(1);
        }

        if(ns_file_close(&file) == NS_ERROR)
        {
            exit(1);
        }
    }

	printf("server: waiting for connections...\n");

    while(1)
    {
        NsSocket client_socket;
        if(ns_socket_get_client(&socket, &client_socket) == NS_ERROR)
        {
            exit(1);
        }

        if(ns_socket_send(&client_socket, webpage, webpage_size) == NS_ERROR) 
        {
            exit(1);
        }

        // for some reason, closing it right after causes the browser to not receive our html. shutdown() fixes that.
        ns_socket_shutdown(&client_socket, NS_SOCKET_RDWR);
        ns_socket_close(&client_socket);
    }

	return 0;
}


#if 0
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>

enum state
{
    LOOKING_IMAGE,
    READING_IMAGE,
};

int main()
{
    state cur_state = LOOKING_IMAGE;

    uint16_t two = 0;
    int size;
    while(1)
    {
        int c = getchar();
        if(c == EOF)
        {
            break;
        }

        switch(cur_state)
        {
            case LOOKING_IMAGE:
            {
                if(two == 0xffd8)
                {
                    printf("start of image detected\n");

                    // set up for next state
                    size = 0;
                    cur_state = READING_IMAGE;
                }
            } break;

            case READING_IMAGE:
            {
                if(two == 0xffd9)
                {
                    printf("end of image detected: %d bytes\n", size);

                    // set up for next state
                    cur_state = LOOKING_IMAGE;
                }
                else
                {
                    size += 1;
                }
            } break;
        }

        two <<= 8;
        two |= c;
    }
}
#endif
