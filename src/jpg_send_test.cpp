#include "ns_thread.h"
#include "ns_socket.h"
#include "ns_websocket.h"
#include "ns_http_server.h"
#include "ns_fork.h"

#include <stdio.h>
#include <signal.h>


#define MAX_CONNECTIONS 8


uint8_t dummy_jpeg_header[] = { 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x01, 0x00, 0x60, 0x00, 0x60, 0x00 };
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

    // fix incorrect 0xfe00 (jpeg-js won't parse it if it's there)

    int i = 0;
    for(; i < size; i++)
    {
        if(frame[i] == 0xfe &&
           frame[i + 1] == 0x00)
        {
            break;
        }
    }

    memcpy(&frame[i], dummy_jpeg_header, sizeof(dummy_jpeg_header));

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
    status = ns_websocket_create(&websocket);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    status = ns_websocket_listen(&websocket, "8001");
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    NsWebSocket peer;
    status = ns_websocket_create(&peer);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

#if 0
    status = ns_websocket_accept(&websocket, &peer);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }
#endif

#if 0
    int child_pid;
    const char *argv[] = {"ffmpeg", "-y", "-f", "v4l2", "-i", "/dev/video0", "-vcodec", "mjpeg", "-f", "mjpeg", "-pix_fmt", "rgba", "-an", "-", NULL};
    status = ns_fork_process("ffmpeg", (char *const *)argv, &child_pid);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    for(;;)
    {
        int frame_size = read_frame(frame, sizeof(frame));
        if(frame_size < 0)
        {
            DebugPrintInfo();
            return status;
        }

        int bytes_sent = ns_websocket_send(&peer, frame, frame_size, BINARY);
        if(bytes_sent != frame_size)
        {
            DebugPrintInfo();
            return status;
        }
    }
#endif
#if 1
    int child_pid;
    const char *argv[] = {"ffmpeg", "-y", "-f", "v4l2", "-i", "/dev/video0", "-vcodec", "mjpeg", "-f", "mjpeg", "-pix_fmt", "rgba", "-an", "-", NULL};
    status = ns_fork_process("ffmpeg", (char *const *)argv, &child_pid);
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    for(int frame_num = 0;; frame_num++)
    {
        int frame_size = read_frame(frame, sizeof(frame));
        if(frame_size < 0)
        {
            DebugPrintInfo();
            return status;
        }

        char *base = (char *)"../test_frames/test_image";
        char *ext = (char *)".jpg";
        char filename[100];
        int len = 0;

        strcpy(filename, base);
        len += strlen(base);
        len += ns_string_from_int(&filename[len], frame_num);
        strcpy(&filename[len], ext);
        len += strlen(ext);

        NsFile image;
        status = ns_file_open_write(&image, filename);
        if(status != NS_SUCCESS)
        {
            DebugPrintInfo();
            return status;
        }

        int written = ns_file_write(&image, frame, frame_size);
        if(written <= 0)
        {
            DebugPrintInfo();
            return status;
        }

        status = ns_file_close(&image);
        if(status != NS_SUCCESS)
        {
            DebugPrintInfo();
            return status;
        }
    }
#else
    NsFile image1;
    status = ns_file_open(&image1, "test_image.jpg");
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    NsFile image2;
    status = ns_file_open(&image2, "test_image2.jpg");
    if(status != NS_SUCCESS)
    {
        DebugPrintInfo();
        return status;
    }

    int bufsize = Kilobytes(100);
    uint8_t *image1_mem = (uint8_t *)malloc(bufsize);
    if(image1_mem == NULL)
    {
        DebugPrintInfo();
        return status;
    }

    uint8_t *image2_mem = (uint8_t *)malloc(bufsize);
    if(image2_mem == NULL)
    {
        DebugPrintInfo();
        return status;
    }

    int image1_size = ns_file_load(&image1, image1_mem, bufsize);
    if(image1_size < 0)
    {
        DebugPrintInfo();
        return status;
    }

    int image2_size = ns_file_load(&image2, image2_mem, bufsize);
    if(image2_size < 0)
    {
        DebugPrintInfo();
        return status;
    }

    int sleep = 100;
    for(;;)
    {
        int bytes_sent = ns_websocket_send(&peer, image1_mem, image1_size, BINARY);
        if(bytes_sent <= 0)
        {
            DebugPrintInfo();
            return status;
        }

        status = ns_thread_sleep(sleep);
        if(status != NS_SUCCESS)
        {
            DebugPrintInfo();
            return status;
        }

        bytes_sent = ns_websocket_send(&peer, image2_mem, image2_size, BINARY);
        if(bytes_sent <= 0)
        {
            DebugPrintInfo();
            return status;
        }

        status = ns_thread_sleep(sleep);
        if(status != NS_SUCCESS)
        {
            DebugPrintInfo();
            return status;
        }
    }

    while(1)
    {
        pause();
    }
#endif

    printf("WE'RE FUCKING DONE BOIS\n");

    return NS_SUCCESS;
}
