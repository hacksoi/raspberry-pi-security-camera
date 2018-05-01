#include "ns_thread.h"
#include "ns_socket.h"
#include "ns_websocket.h"
#include "ns_http_server.h"
#include "ns_fork.h"

#include <stdio.h>
#include <signal.h>


#define MAX_CONNECTIONS 2


uint8_t frame[Kilobytes(64)];


enum State
{
    LOOKING_IMAGE,
    READING_IMAGE,
};

int
read_frame(uint8_t *dest, int dest_size)
{
    State cur_state = LOOKING_IMAGE;

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

                    cur_state = READING_IMAGE;

                    size = 0;
                    dest[size++] = 0xff;
                    dest[size++] = 0xd8;
                }
            } break;

            case READING_IMAGE:
            {
                if(size >= dest_size)
                {
                    DebugPrintInfo();
                    return NS_ERROR;
                }

                dest[size++] = c;

                if(two == 0xffd9)
                {
                    printf("end of image detected: %d bytes\n", size);

                    //cur_state = LOOKING_IMAGE;
                    goto done;
                }
            } break;
        }

        two <<= 8;
        two |= c;
    }

done:
    return size;
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

    status = ns_websockets_startup(MAX_CONNECTIONS, 3);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return NS_ERROR;
    }

    status = ns_http_server_startup("8000", MAX_CONNECTIONS, 4);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    NsWebSocket websocket;
    status = ns_websocket_listen(&websocket, "8001");
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    NsWebSocket peer;
    status = ns_websocket_accept(&websocket, &peer);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

#if 0
    for(int i = 0; i < 20 ; i++)
    {
        frame[i] = i;
    }

    int bytes_sent = ns_websocket_send(&peer, frame, 20, BINARY);
    if(bytes_sent != 20)
    {
        DebugPrintInfo();
        return status;
    }
#else
    int child_pid;
    const char *argv[] = {"ffmpeg", "-y", "-f", "v4l2", "-i", "/dev/video0", "-vcodec", "mjpeg", "-f", "mjpeg", "-pix_fmt", "rgba", "-an", "-", NULL};
    status = ns_fork_process("ffmpeg", (char *const *)argv, &child_pid);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    int frame_size = read_frame(frame, sizeof(frame));
    if(frame_size < 0)
    {
        DebugPrintInfo();
        return status;
    }

    // kill ffmpeg
    status = kill(child_pid, SIGTERM);
    if(status != 0)
    {
        DebugPrintOsInfo();
        return status;
    }

    int bytes_sent = ns_websocket_send(&peer, frame, frame_size, BINARY);
    if(bytes_sent != frame_size)
    {
        DebugPrintInfo();
        return status;
    }

#endif
    printf("WE'RE FUCKING DONE BOIS\n");

    return NS_SUCCESS;
}
