#if 0
TODO:
    -ping pong
    -alert client websocket thread about closure from background thread
#endif

#include "common.h"
#include "posix_wrappers.h"
#include "message_buffer.h"

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

/* preprocessor directives */
//{

#define WEBSOCKET_KEY_HEADER "Sec-WebSocket-Key: "
#define WEBSOCKET_KEY_HEADER_LENGTH strlen(WEBSOCKET_KEY_HEADER)

#define WEBSERVER_PORT "3490"
#define VIDEO_STREAM_PORT "3491"

#define BACKLOG 10

#define OPCODE_CONNECTION_CLOSE 0x08
//}

/* global variables */
//{

// TODO: sync this
pthread_t thread_pool[20];
int thread_pool_size;
//}


/* sockets */
//{

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if(sa->sa_family == AF_INET)
    {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int create_socket(const char *port)
{
    int status;

	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

    addrinfo *servinfo;
    status = getaddrinfo(NULL, port, &hints, &servinfo);
	if(status != 0)
    {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		return -1;
	}

    int sock_fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if(sock_fd == -1)
    {
        return -1;
    }

    int yes = 1;
    if(setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
        return -1;
    }

    if(bind(sock_fd, servinfo->ai_addr, servinfo->ai_addrlen) == -1)
    {
        return -1;
    }

	freeaddrinfo(servinfo); // all done with this structure

	if(listen(sock_fd, BACKLOG) == -1)
    {
        return -1;
	}

    return sock_fd;
}

int get_client(int sock_fd, const char *name = NULL)
{
    sockaddr_storage their_addr;
    socklen_t sin_size = sizeof(their_addr);
    int client_fd = accept(sock_fd, (sockaddr *)&their_addr, &sin_size);
    if(client_fd == -1)
    {
        return -1;
    }

    if(name != NULL)
    {
        char s[INET6_ADDRSTRLEN];
        inet_ntop(their_addr.ss_family, get_in_addr((sockaddr *)&their_addr), s, sizeof(s));
        printf("%s: got connection from %s\n", name, s);
    }

    return client_fd;
}
//}

/* websockets */
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
    int frame_length;
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
            frame_length = 2;
        }
        else
        {
            put64be(&frame[2], message_length);
            payload = &frame[10];
            frame_length = 10;
        }

        // copy payload
        memcpy(payload, message, message_length);
        frame_length += message_length;
    }

    if(send(socket_fd, frame, frame_length, 0) == -1)
    {
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
    int frame_length;
    {
        // set fin bit
        frame[0] |= 0x80;

        // set opcode to close
        frame[0] |= 0x08;

        frame_length = 2;
    }

    if(send(socket_fd, frame, frame_length, 0) == -1)
    {
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

void print(WebSocketFrame frame)
{
    printf("fin: 0x%x, rsv1: 0x%x, rsv2: 0x%x, rsv3: 0x%x, opcode: 0x%x, mask: 0x%x, "
           "payload_length: %llu, mask[0]: 0x%x, mask[1]: 0x%x, mask[2]: 0x%x, mask[3]: 0x%x\n", 
           frame.fin, frame.rsv1, frame.rsv2, frame.rsv3, frame.opcode, frame.mask, 
           frame.payload_length, frame.mask_key[0], frame.mask_key[1], frame.mask_key[2], frame.mask_key[3]);
}

void *background_websocket_client_thread_entry(void *thread_data)
{
    WebSocket *socket = (WebSocket *)thread_data;
    int socket_fd = socket->fd;

    while(1)
    {
        uint8_t raw_frame[1024];
        int frame_length = recv(socket_fd, (char *)raw_frame, sizeof(raw_frame), 0);
        if(frame_length < 0)
        {
            exit(1);
        }

        const WebSocketFrame frame = inflate_frame(raw_frame);

        if(frame.opcode == OPCODE_CONNECTION_CLOSE)
        {
            printf("received a close\n");
            send_close(socket);
            return 0;
        }

        // decode
        for(uint32_t i = 0; i < frame.payload_length; i++)
        {
            frame.payload[i] ^= frame.mask_key[i % 4];
        }

        if(add(&socket->message_buffer, frame.payload, frame.payload_length) != (int)frame.payload_length)
        {
            printf("failed to add message\n");
        }
    }

    return 0;
}
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
            exit(1);
        }

        printf("test message: %s\n", test_message);

        if(strcmp((char *)test_message, "this is a test"))
        {
            fprintf(stderr, "websocket basic test failed\n");
            exit(1);
        }

        strcat((char *)test_message, " - received!");
        if(!send(socket, test_message, strlen((char *)test_message)))
        {
            exit(1);
        }
    }

    printf("basic test passed\n");
#endif

#if 0
    while(1)
    {
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
    }
#endif

    return 0;
}

void *stream_thread_entry(void *data)
{
    WebSocket socket;
    if(!create(&socket, VIDEO_STREAM_PORT))
    {
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
        exit(1);
    }

    char *webpage;
    int webpage_size;
    if(!load_file("webpage.html", &webpage, &webpage_size))
    {
        exit(1);
    }

	printf("server: waiting for connections...\n");

    while(1)
    {
        int client_fd = get_client(sock_fd, "webserver");
        if(client_fd == -1)
        {
            exit(1);
        }

        if(send(client_fd, webpage, webpage_size, 0) == -1) 
        {
            exit(1);
        }
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
