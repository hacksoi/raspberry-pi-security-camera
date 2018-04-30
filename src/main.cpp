#include "ns_common.h"
#include "ns_message_queue.h"
#include "ns_socket.h"
#include "ns_socket_pool.h"
#include "ns_websocket.h"
#include "ns_file.h"
#include "ns_http_server.h"


#define NUM_WORKER_THREADS 3
#define MAX_CONNECTIONS 64

#define HTTP_SERVER_PORT "8000"
#define VIDEO_STREAM_PORT "3491"


struct VsWebSocket
{
    NsWebSocket websocket;
    VsWebSocket *next;
    VsWebSocket *prev;
};

struct VsWebSocketList
{
    VsWebSocket *head;
    NsCondv condv;
    NsMutex mutex;
    int size;
};


global NsWorkerThreads worker_threads;
global VsWebSocketList vs_websocket_lists[NUM_WORKER_THREADS];
global VsWebSocket vs_websockets[MAX_CONNECTIONS];
global VsWebSocket *vs_websocket_free_list_head;


int
init_list(VsWebSocketList *list)
{
    int status;

    status = ns_condv_create(&list->condv);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    status = ns_mutex_create(&list->mutex);;
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    return NS_SUCCESS;
}

VsWebSocket *
get_websocket()
{
    VsWebSocket *free = vs_websocket_free_list_head;
    if(free == NULL)
    {
        DebugPrintInfo();
        return NULL;
    }

    vs_websocket_free_list_head = free->next;

    return free;
}

int
add_websocket(VsWebSocketList *list, VsWebSocket *websocket)
{
    int status;

    status = ns_mutex_lock(&list->mutex);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    VsWebSocket *head = list->head;

    // empty?
    if(head == NULL)
    {
        list->head = websocket;
    }
    else
    {
        head->prev = websocket;
        websocket->next = head;
        websocket->prev = NULL;
        list->head = websocket;
    }

    list->size++;

    status = ns_condv_signal(&list->condv);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    status = ns_mutex_unlock(&list->mutex);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    return NS_SUCCESS;
}

int
remove_websocket(VsWebSocketList *list, VsWebSocket *websocket)
{
    int status;

    status = ns_mutex_lock(&list->mutex);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    VsWebSocket *head = list->head;

    if(head == NULL)
    {
        DebugPrintInfo();
        return false;
    }

    VsWebSocket *prev = websocket->prev;
    VsWebSocket *next = websocket->next;

    // head?
    if(head == websocket)
    {
        // single element?
        if(head->next == NULL)
        {
            list->head = NULL;
        }
        else
        {
            next->prev = NULL;
            list->head = next;
        }
    }
    // tail?
    else if(websocket->next == NULL)
    {
        prev->next = NULL;
    }
    else
    {
        prev->next = next;
        next->prev = prev;
    }

    list->size--;

    // add to free list
    websocket->next = vs_websocket_free_list_head;
    vs_websocket_free_list_head = websocket;

    status = ns_mutex_unlock(&list->mutex);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    return NS_SUCCESS;
}

void *
video_stream_peer_sender_thread_entry(void *thread_data)
{
    VsWebSocketList *list = (VsWebSocketList *)thread_data;

    while(1)
    {
        ns_mutex_lock(&list->mutex);

        // wait for list to be non empty
        while(list->size == 0)
        {
            ns_condv_wait(&list->condv, &list->mutex);
        }

        VsWebSocket *cur_vs_websocket = list->head;
        while(cur_vs_websocket != NULL)
        {
            NsWebSocket *websocket = &cur_vs_websocket->websocket;

            char *message = (char *)"funny cat photos";
            int bytes_sent = ns_websocket_send(websocket, message, (strlen(message) + 1));
            if(bytes_sent == NS_WEBSOCKET_PEER_CLOSED)
            {
                printf("closing peer...\n");
                remove_websocket(list, cur_vs_websocket);
            }
            else if(bytes_sent <= 0)
            {
                DebugPrintInfo();
                return (void *)bytes_sent;
            }

            cur_vs_websocket = cur_vs_websocket->next;
        }

        ns_mutex_unlock(&list->mutex);

        usleep(500000);
    }

    return NS_SUCCESS;
}

void *
video_stream_peer_getter_thread_entry(void *data)
{
    int status;

    NsWebSocket websocket;
    status = ns_websocket_listen(&websocket, VIDEO_STREAM_PORT);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return (void *)status;
    }

    while(1)
    {
        VsWebSocket *peer_vs_websocket = get_websocket();
        if(peer_vs_websocket == NULL)
        {
            DebugPrintInfo();
            return (void *)status;
        }

        NsWebSocket *peer_websocket = &peer_vs_websocket->websocket;
        status = ns_websocket_get_peer(&websocket, peer_websocket);
        if(status != NS_SUCCESS)
        {
            DebugPrintInfo();
            return (void *)status;
        }

        // do basic test

        printf("performing basic test...\n");

        char test_message[256];
        int message_length = ns_websocket_receive(peer_websocket, test_message, sizeof(test_message));
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
        int bytes_sent = ns_websocket_send(peer_websocket, test_message, strlen(test_message));
        if(bytes_sent <= 0)
        {
            DebugPrintInfo();
            return (void *)NS_ERROR;
        }

        printf("basic test passed\n");

        // add peer to list

        int smallest_idx = 0;
        for(int i = 0; i < NUM_WORKER_THREADS; i++)
        {
            if(vs_websocket_lists[i].size < vs_websocket_lists[smallest_idx].size)
            {
                smallest_idx = i;
            }
        }

        status = add_websocket(&vs_websocket_lists[smallest_idx], peer_vs_websocket);
        if(status != NS_SUCCESS)
        {
            DebugPrintInfo();
            return (void *)status;
        }
    }

    return NS_SUCCESS;
}

int 
main()
{
    int status;

    status = ns_sockets_startup();
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

#define MAX_CONNECTIONS 64

    status = ns_websockets_startup(MAX_CONNECTIONS, 4);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return NS_ERROR;
    }

    status = ns_http_server_startup(MAX_CONNECTIONS, HTTP_SERVER_PORT, 4);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    status = ns_worker_threads_create(&worker_threads, NUM_WORKER_THREADS, MAX_CONNECTIONS);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return NS_ERROR;
    }

    // initialize free list
    vs_websocket_free_list_head = vs_websockets;
    for(int i = 0; i < (int)(ArrayCount(vs_websockets) - 1); i++)
    {
        vs_websockets[i].next = &vs_websockets[i + 1];
    }

    for(int i = 0; i < NUM_WORKER_THREADS; i++)
    {
        VsWebSocketList *list = &vs_websocket_lists[i];
        init_list(list);
        status = ns_worker_threads_add_work(&worker_threads, 
                                            video_stream_peer_sender_thread_entry, 
                                            (void *)list);
        if(status != NS_SUCCESS)
        {
            DebugPrintInfo();
            return status;
        }
    }

    video_stream_peer_getter_thread_entry(NULL);

	return NS_SUCCESS;
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
