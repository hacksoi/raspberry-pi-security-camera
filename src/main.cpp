#if 0
TODO:
    -websockets.h
        -alert client websocket thread about closure from background thread
#endif

#include "common.h"
#include "message_buffer.h"
#include "socket_wrapper.h"
#include "websockets.h"

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

/* preprocessor directives */
//{

#define WEBSERVER_PORT "3490"
#define VIDEO_STREAM_PORT "3491"
//}

void *websocket_client_thread_entry(void *thread_data)
{
    WebSocket *socket = (WebSocket *)thread_data;

#if 1
    // perform basic test
    {
        uint8_t test_message[256];
        int message_length = recv(socket, test_message, sizeof(test_message));
        if(message_length == -1)
        {
            DEBUG_PRINT_INFO();
            exit(1);
        }

        printf("test message: %s\n", test_message);

        if(strcmp((char *)test_message, "this is a test"))
        {
            DEBUG_PRINT_INFO();
            exit(1);
        }

        strcat((char *)test_message, " - received!");
        if(!send(socket, test_message, strlen((char *)test_message)))
        {
            DEBUG_PRINT_INFO();
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
        if(!send(socket, (uint8_t *)message, (strlen(message) + 1)))
        {
            DEBUG_PRINT_INFO();
            exit(1);
        }

        usleep(500000);
    }
#endif

    return 0;
}

void *stream_thread_entry(void *data)
{
    WebSocket socket;
    if(!create(&socket, VIDEO_STREAM_PORT))
    {
        DEBUG_PRINT_INFO();
        exit(1);
    }

    WebSocket clients[10];
    int num_clients = 0;
    while(1)
    {
        get_client(&socket, &clients[num_clients]);
        pthread_create(&thread_pool[thread_pool_size++], NULL, websocket_client_thread_entry, &clients[num_clients++]);
    }

    return 0;
}

int main(void)
{
    pthread_create(&thread_pool[thread_pool_size++], NULL, stream_thread_entry, NULL);

    int sock_fd = create_socket(WEBSERVER_PORT);
    if(sock_fd == -1)
    {
        DEBUG_PRINT_INFO();
        exit(1);
    }

    char *webpage;
    int webpage_size;
    if(!load_file("webpage.html", &webpage, &webpage_size))
    {
        DEBUG_PRINT_INFO();
        exit(1);
    }

	printf("server: waiting for connections...\n");

    while(1)
    {
        int client_fd = get_client(sock_fd, 0, "webserver");
        if(client_fd == -1)
        {
            DEBUG_PRINT_INFO();
            exit(1);
        }

        if(send(client_fd, webpage, webpage_size, 0) == -1) 
        {
            DEBUG_PRINT_INFO();
            exit(1);
        }

        // for some reason, closing it right after causes the browser to not receive our html. shutdown() fixes that.
        shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
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
