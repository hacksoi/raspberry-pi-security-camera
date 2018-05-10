#if 0
TODO: handle peer disconnecting by switching from wifi to mobile data
#endif

#include "ns_common.h"
#include "ns_message_queue.h"
#include "ns_socket.h"
#include "ns_socket_pool.h"
#include "ns_websocket.h"
#include "ns_file.h"
#include "ns_http_server.h"
#include "ns_fork.h"


#define MAX_CONNECTIONS 64

#define HTTP_SERVER_PORT "8090"
#define VIDEO_STREAM_PORT "8091"


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
};


global NsThread peer_sender_thread;
global VsWebSocketList vs_websocket_list;
global VsWebSocket vs_websockets[MAX_CONNECTIONS];
global VsWebSocket *vs_websocket_free_list_head;

uint8_t frame[Kilobytes(512)];
int frame_size;


int
read_frame(uint8_t *dst, int dst_size)
{
    int status;

    int size;
    uint16_t two = 0;
    bool found_start = false;
    while(1)
    {
        int c = getchar();
        if(c == EOF)
        {
            DebugPrintInfo();
            return NS_ERROR;
        }

        two <<= 8;
        two |= c;

        if(!found_start)
        {
            if(two == 0xffd8)
            {
                found_start = true;

                dst[size++] = 0xff;
                dst[size++] = 0xd8;
            }
        }
        else
        {
            if(size >= dst_size)
            {
                DebugPrintInfo();
                return NS_ERROR;
            }

            dst[size++] = c;

            if(two == 0xffd9)
            {
                printf("end of image detected: %d bytes\n", size);
                break;
            }
        }
    }

    // fix incorrect 0xfe00 (jpeg-js won't parse it if it's there)

    int i = 0;
    for(; i < size; i++)
    {
        if(dst[i] == 0xfe &&
           dst[i + 1] == 0x00)
        {
            break;
        }
    }

    uint8_t dummy_jpeg_header[] = { 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x01, 0x00, 0x60, 0x00, 0x60, 0x00 };
    memcpy(&dst[i], dummy_jpeg_header, sizeof(dummy_jpeg_header));

    return size;
}

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
        websocket->next = NULL;
        websocket->prev = NULL;
        list->head = websocket;
    }
    else
    {
        head->prev = websocket;
        websocket->next = head;
        websocket->prev = NULL;
        list->head = websocket;
    }

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

/* This MUST only be called from within the client handler thread. */
int
remove_websocket(VsWebSocketList *list, VsWebSocket *websocket)
{
    int status;

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

    // add to free list
    websocket->next = vs_websocket_free_list_head;
    vs_websocket_free_list_head = websocket;

    return NS_SUCCESS;
}

void *
peer_sender_thread_entry(void *thread_data)
{
    int status;

    int child_pid;
    const char *argv[] = {"ffmpeg", "-y", "-f", "v4l2", "-i", "/dev/video0", "-vcodec", "mjpeg", "-r", "5", "-f", "mjpeg", "-an", "-", NULL};
    status = ns_fork_process("ffmpeg", (char *const *)argv, &child_pid);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return (void *)status;
    }

    while(1)
    {
        int frame_size = read_frame(frame, sizeof(frame));
        if(frame_size <= 0)
        {
            DebugPrintInfo();
            return (void *)frame_size;
        }

        status = ns_mutex_lock(&vs_websocket_list.mutex);
        if(status != NS_SUCCESS)
        {
            DebugPrintInfo();
            return (void *)status;
        }

        if(vs_websocket_list.head != NULL)
        {
            VsWebSocket *cur_vs_websocket = vs_websocket_list.head;
            while(cur_vs_websocket != NULL)
            {
                NsWebSocket *websocket = &cur_vs_websocket->websocket;
                VsWebSocket *next = cur_vs_websocket->next;

                int bytes_sent = ns_websocket_send(websocket, frame, frame_size);
                if(bytes_sent != frame_size)
                {
                    if(bytes_sent == 0)
                    {
                        printf("main: closing websocket...\n");

                        status = ns_websocket_destroy(websocket);
                        if(status != NS_SUCCESS)
                        {
                            DebugPrintInfo();
                            return (void *)status;
                        }

                        status = remove_websocket(&vs_websocket_list, cur_vs_websocket);
                        if(status != NS_SUCCESS)
                        {
                            DebugPrintInfo();
                            return (void *)status;
                        }

                        printf("main: websocket closed\n");
                    }
                    else
                    {
                        DebugPrintInfo();
                        return (void *)bytes_sent;
                    }
                }

                cur_vs_websocket = next;
            }
        }

        status = ns_mutex_unlock(&vs_websocket_list.mutex);
        if(status != NS_SUCCESS)
        {
            DebugPrintInfo();
            return (void *)status;
        }
    }

    return (void *)NS_SUCCESS;
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
        status = ns_websocket_accept(&websocket, peer_websocket);
        if(status != NS_SUCCESS)
        {
            DebugPrintInfo();
            return (void *)status;
        }

        printf("main: received a peer\n");

#if 0
        // do basic test

        char test_message[256];
        int message_length = ns_websocket_receive(peer_websocket, test_message, sizeof(test_message));
        if(message_length <= 0)
        {
            DebugPrintInfo();
            return (void *)NS_ERROR;
        }
        test_message[message_length] = '\0';

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
#endif

        status = add_websocket(&vs_websocket_list, peer_vs_websocket);
        if(status != NS_SUCCESS)
        {
            DebugPrintInfo();
            return (void *)status;
        }
    }

    DebugPrintInfo();
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

    status = ns_http_server_startup(HTTP_SERVER_PORT, MAX_CONNECTIONS, 4);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    // initialize free list
    vs_websocket_free_list_head = vs_websockets;
    for(int i = 0; i < (int)(ArrayCount(vs_websockets) - 1); i++)
    {
        vs_websockets[i].next = &vs_websockets[i + 1];
    }

    status = ns_thread_create(&peer_sender_thread, peer_sender_thread_entry, NULL);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    video_stream_peer_getter_thread_entry(NULL);

	return NS_SUCCESS;
}
