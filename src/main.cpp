#if 0
#endif

#include "ns_common.h"
#include "ns_message_queue.h"
#include "ns_socket.h"
#include "ns_websocket.h"
#include "ns_file.h"
#include "ns_http_server.h"

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


NsThreadPool thread_pool;


void *
video_stream_client_thread_entry(void *thread_data)
{
    NsWebSocket *websocket = (NsWebSocket *)thread_data;

#if 1
    {
        printf("performing basic test...\n");

        char test_message[256];
        int message_length = ns_websocket_receive(websocket, test_message, sizeof(test_message));
        if(message_length <= 0)
        {
            DebugPrintInfo();
            return (void *)NS_ERROR;
        }

        printf("test message: %s\n", test_message);

        if(strcmp(test_message, "this is a test"))
        {
            DebugPrintInfo();
            return (void *)NS_ERROR;
        }

        strcat(test_message, " - received!");
        int bytes_sent = ns_websocket_send(websocket, test_message, strlen(test_message));
        if(bytes_sent <= 0)
        {
            DebugPrintInfo();
            return (void *)NS_ERROR;
        }

        printf("basic test passed\n");
    }
#endif

    while(1)
    {
        char *message = (char *)"funny cat photos";
        int bytes_sent = ns_websocket_send(websocket, message, (strlen(message) + 1));
        if(bytes_sent == NS_WEBSOCKET_CLIENT_CLOSED)
        {
            printf("closing thread...\n");
            return (void *)NS_SUCCESS;
        }
        else if(bytes_sent <= 0)
        {
            DebugPrintInfo();
            return (void *)bytes_sent;
        }

        usleep(500000);
    }

    return 0;
}

void *
video_stream_client_getter_thread_entry(void *data)
{
    int status;
    NsWebSocket websocket;

    if(ns_websocket_create(&websocket, VIDEO_STREAM_PORT) != NS_SUCCESS)
    {
        DebugPrintInfo();
        exit(1);
    }

    NsWebSocket clients[10];
    int num_clients = 0;
    while(1)
    {
        status = ns_websocket_get_client(&websocket, &clients[num_clients]);
        if(status != NS_SUCCESS)
        {
            DebugPrintInfo();
            exit(1);
        }

        NsThread *client_thread;
        status = ns_thread_pool_get(&thread_pool, &client_thread);
        if(status != NS_SUCCESS)
        {
            DebugPrintInfo();
            exit(1);
        }

        status = ns_thread_pool_create_thread(&thread_pool, video_stream_client_thread_entry, 
                                              &clients[num_clients++]);
        if(status != NS_SUCCESS)
        {
            DebugPrintInfo();
            exit(1);
        }
    }

    return 0;
}

int 
main(void)
{
    int status;

    status = ns_websockets_init();
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        exit(1);
    }

    status = ns_thread_pool_create(&thread_pool, 256);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        exit(1);
    }

    status = ns_thread_pool_create_thread(&thread_pool, video_stream_client_getter_thread_entry, NULL);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        exit(1);
    }

    NsHttpServer http_server;
    ns_http_server_create(&http_server);

    for(;;)
    {
        if(pause() == -1)
        {
            exit(1);
        }
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
