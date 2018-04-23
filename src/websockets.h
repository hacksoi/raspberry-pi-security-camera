#if 0
// TODO:
    -instead of making a thread per client (over 1000 threads?) keep a table of clients and iterate over them.
#endif

#ifndef WEBSOCKETS_H
#define WEBSOCKETS_H

#include "common.h"
#include "message_buffer.h"
#include "socket_wrapper.h"

#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>

/* preprocessor directives */
//{

#define WEBSOCKET_KEY_HEADER "Sec-WebSocket-Key: "
#define WEBSOCKET_KEY_HEADER_LENGTH strlen(WEBSOCKET_KEY_HEADER)

#define OPCODE_CONNECTION_CLOSE 0x08
#define OPCODE_PING 0x09
#define OPCODE_PONG 0x0A
//}

/* global variables */
//{

// TODO: sync this
pthread_t thread_pool[20];
int thread_pool_size;
//}

/* structs */
//{

struct WebSocket
{
    int fd;
    MessageBuffer message_buffer;
};

struct WebSocketFrame
{
    bool fin;
    bool rsv1;
    bool rsv2;
    bool rsv3;
    uint32_t opcode;
    bool mask;
    uint64_t payload_length;
    uint8_t *mask_key; // 4 consecutive bytes
    uint8_t *payload;
};
//}

void print(WebSocketFrame frame)
{
    printf("fin: 0x%x, rsv1: 0x%x, rsv2: 0x%x, rsv3: 0x%x, opcode: 0x%x, mask: 0x%x, "
           "payload_length: %llu, mask[0]: 0x%x, mask[1]: 0x%x, mask[2]: 0x%x, mask[3]: 0x%x\n", 
           frame.fin, frame.rsv1, frame.rsv2, frame.rsv3, frame.opcode, frame.mask, 
           frame.payload_length, frame.mask_key[0], frame.mask_key[1], frame.mask_key[2], frame.mask_key[3]);
}

WebSocketFrame inflate_frame(uint8_t *raw_frame)
{
    WebSocketFrame frame;
    {
        frame.fin = (raw_frame[0] & 0x80);
        frame.rsv1 = (raw_frame[0] & 0x40);
        frame.rsv2 = (raw_frame[0] & 0x20);
        frame.rsv3 = (raw_frame[0] & 0x10);
        frame.opcode = (raw_frame[0] & 0x0f);
        frame.mask = (raw_frame[1] & 0x80);

        // calculate payload length
        {
            frame.payload_length = (raw_frame[1] & 0x7f);
            if(frame.payload_length <= 125)
            {
                frame.mask_key = &raw_frame[2];
            }
            else if(frame.payload_length == 126)
            {
                frame.payload_length = get16be(&raw_frame[2]);
                frame.mask_key = &raw_frame[4];
            }
            else if(frame.payload_length == 127)
            {
                frame.payload_length = get64be(&raw_frame[2]);
                frame.mask_key = &raw_frame[10];
            }
        }

        frame.payload = (frame.mask_key + 4);
    }
    return frame;
}

bool init(WebSocket *socket, int fd)
{
    socket->fd = fd;
    if(!init(&socket->message_buffer))
    {
        return false;
    }
    return true;
}

bool create(WebSocket *socket, const char *port)
{
    int socket_fd = create_socket(port);
    if(socket_fd == -1)
    {
        return false;
    }

    if(!init(socket, socket_fd))
    {
        return false;
    }

    return true;
}

int recv(WebSocket *socket, uint8_t *dest, uint32_t dest_size)
{
    int message_length = get(&socket->message_buffer, dest, dest_size);
    return message_length;
}

bool send(WebSocket *socket, uint8_t *message, uint32_t message_length)
{
    int socket_fd = socket->fd;

    uint8_t frame[256] = {};
    assert(message_length < sizeof(frame));

    // fill out frame
    int bytes_received;
    {
        // set fin bit
        frame[0] |= 0x80;

        // set opcode to text
        frame[0] |= 0x01;

        // set payload length
        uint8_t *payload;
        if(message_length <= 125)
        {
            frame[1] |= message_length;
            payload = &frame[2];
            bytes_received = 2;
        }
        else
        {
            put64be(&frame[2], message_length);
            payload = &frame[10];
            bytes_received = 10;
        }

        // copy payload
        memcpy(payload, message, message_length);
        bytes_received += message_length;
    }

    if(send(socket_fd, frame, bytes_received, 0) == -1)
    {
        DEBUG_PRINT_INFO();
        return false;
    }

    return true;
}

void *background_websocket_client_thread_entry(void *thread_data);
bool get_client(WebSocket *socket, WebSocket *client_socket)
{
    const int socket_fd = socket->fd;
    const int client_fd = get_client(socket_fd);
    if(client_fd == -1)
    {
        return -1;
    }

    char client_handshake[4096];
    int client_handshake_length = 0;
    {
        int client_handshake_length = recv(client_fd, client_handshake, sizeof(client_handshake), 0);
        if(client_handshake_length == -1)
        {
            DEBUG_PRINT_INFO();
            return -1;
        }

        client_handshake[client_handshake_length] = 0;
    }

    char client_handshake_reply[512] =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: ";
    {
        char reply_key[128];
        {
            char client_key[128];
            {
                char *start_of_key = NULL;
                for(unsigned int i = 0; i < (client_handshake_length - WEBSOCKET_KEY_HEADER_LENGTH); i++)
                {
                    if(!strncmp(&client_handshake[i], WEBSOCKET_KEY_HEADER, WEBSOCKET_KEY_HEADER_LENGTH))
                    {
                        start_of_key = &client_handshake[i + WEBSOCKET_KEY_HEADER_LENGTH];
                        break;
                    }
                }

                // place key in buffer
                int length = 0;
                while(*start_of_key != '\r')
                {
                    client_key[length++] = *start_of_key++;
                }
                client_key[length] = 0;
            }

            strcat(client_key, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

            if(!sha1(client_key, reply_key))
            {
                return -1;
            }
        }

        // fill out key for our handshake reply
        int handshake_reply_length = strlen(client_handshake_reply);
        strcat(&client_handshake_reply[handshake_reply_length], reply_key);
        handshake_reply_length += strlen(reply_key);
        client_handshake_reply[handshake_reply_length++] = '\r';
        client_handshake_reply[handshake_reply_length++] = '\n';
        client_handshake_reply[handshake_reply_length++] = '\r';
        client_handshake_reply[handshake_reply_length++] = '\n';
        client_handshake_reply[handshake_reply_length++] = 0;
    }

    if(send(client_fd, client_handshake_reply, strlen(client_handshake_reply), 0) == -1) 
    {
        DEBUG_PRINT_INFO();
        return -1;
    }

    if(!init(client_socket, client_fd))
    {
        return false;
    }

    pthread_create(&thread_pool[thread_pool_size++], NULL, background_websocket_client_thread_entry, client_socket);

    return true;
}

bool send_close(WebSocket *socket)
{
    int socket_fd = socket->fd;

    uint8_t frame[2] = {};
    int bytes_received;
    {
        // set fin bit
        frame[0] |= 0x80;

        // set opcode to close
        frame[0] |= 0x08;

        bytes_received = 2;
    }

    if(send(socket_fd, frame, bytes_received, 0) == -1)
    {
        DEBUG_PRINT_INFO();
        return false;
    }

    return true;
}

bool close(WebSocket *socket)
{
    int socket_fd = socket->fd;

    send_close(socket);

    // receive frames until we receive the close frame response
    while(1)
    {
        uint8_t frame[256];
        if(recv(socket_fd, frame, sizeof(frame), 0) == -1)
        {
            DEBUG_PRINT_INFO();
            return false;
        }

        if((frame[0] & 0x0f) == 0x08)
        {
            break;
        }
    }

    close(socket_fd);

    return true;
}

void *background_websocket_client_thread_entry(void *thread_data)
{
    WebSocket *socket = (WebSocket *)thread_data;
    int socket_fd = socket->fd;

    while(1)
    {
        uint8_t raw_frame[1024];
        int bytes_received = recv(socket_fd, (char *)raw_frame, sizeof(raw_frame), 0);
        if(bytes_received < 0)
        {
            DEBUG_PRINT_INFO();
            exit(1);
        }

        WebSocketFrame frame = inflate_frame(raw_frame);
        switch(frame.opcode)
        {
            case OPCODE_CONNECTION_CLOSE:
            {
                printf("received a close\n");
                send_close(socket);
                return 0;
            } break;

            case OPCODE_PING:
            {
                printf("received a ping\n");

                // send pong
                {
                    // change opcode to pong
                    {
                        raw_frame[0] &= ~OPCODE_PING;
                        raw_frame[0] |= OPCODE_PONG;
                    }

                    int bytes_sent = send(socket_fd, raw_frame, bytes_received, 0);
                    if(bytes_sent != bytes_received)
                    {
                        DEBUG_PRINT_INFO();
                        return 0;
                    }
                }
            } break;

            default:
            {
                // decode
                for(uint32_t i = 0; i < frame.payload_length; i++)
                {
                    frame.payload[i] ^= frame.mask_key[i % 4];
                }

                int bytes_added = add(&socket->message_buffer, frame.payload, frame.payload_length);
                if(bytes_added != (int)frame.payload_length)
                {
                    DEBUG_PRINT_INFO();
                }
            } break;
        }
    }

    return 0;
}

#endif
