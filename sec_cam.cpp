#if 0
TODO:
    -ping pong
    -alert client websocket thread about closure from background thread
#endif

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

#define ArrayCount(array) (sizeof(array)/sizeof(array[0]))
#define KILOBYTES(x) (x*1024)
#define MEGABYTES(x) (x*1024*1024)

#define WEBSOCKET_KEY_HEADER "Sec-WebSocket-Key: "
#define WEBSOCKET_KEY_HEADER_LENGTH strlen(WEBSOCKET_KEY_HEADER)

#define WEBSERVER_PORT "3490"
#define VIDEO_STREAM_PORT "3491"

#define BACKLOG 10

#define OPCODE_CONNECTION_CLOSE 0x08

// TODO: sync this
pthread_t thread_pool[20];
int thread_pool_size;

char semaphore_name[] = "aaaaaaaaaa";

/* wrappers */
//{

#define MAKE_WRAPPER1(return_value, name, t1, n1) \
    return_value name##_(t1 n1) \
    { \
        int result = name(n1);\
        if(result == -1) \
        { \
            perror(#name "() failed"); \
        } \
        return result; \
    }

#define MAKE_WRAPPER2(return_value, name, t1, n1, t2, n2) \
    return_value name##_(t1 n1, t2 n2) \
    { \
        int result = name(n1, n2);\
        if(result == -1) \
        { \
            perror(#name "() failed"); \
        } \
        return result; \
    }

#define MAKE_WRAPPER3(return_value, name, t1, n1, t2, n2, t3, n3) \
    return_value name##_(t1 n1, t2 n2, t3 n3) \
    { \
        int result = name(n1, n2, n3);\
        if(result == -1) \
        { \
            perror(#name "() failed"); \
        } \
        return result; \
    }

#define MAKE_WRAPPER4(return_value, name, t1, n1, t2, n2, t3, n3, t4, n4) \
    return_value name##_(t1 n1, t2 n2, t3 n3, t4 n4) \
    { \
        int result = name(n1, n2, n3, n4);\
        if(result == -1) \
        { \
            perror(#name "() failed"); \
        } \
        return result; \
    }

#define MAKE_WRAPPER5(return_value, name, t1, n1, t2, n2, t3, n3, t4, n4, t5, n5) \
    return_value name##_(t1 n1, t2 n2, t3 n3, t4 n4, t5 n5) \
    { \
        int result = name(n1, n2, n3, n4, n5);\
        if(result == -1) \
        { \
            perror(#name "() failed"); \
        } \
        return result; \
    }

MAKE_WRAPPER1(int, sem_post, sem_t *, sem);
MAKE_WRAPPER1(int, sem_wait, sem_t *, sem);
MAKE_WRAPPER1(int, close, int, fd);
MAKE_WRAPPER2(int, listen, int, sockfd, int, backlog);
MAKE_WRAPPER3(int, socket, int, domain, int, type, int, protocol);
MAKE_WRAPPER3(int, bind, int, sockfd, const struct sockaddr *, addr, socklen_t, addrlen);
MAKE_WRAPPER3(int, accept, int, sockfd, struct sockaddr *, addr, socklen_t *, addrlen);
MAKE_WRAPPER4(ssize_t, recv, int, sockfd, void *, buf, size_t, len, int, flags);
MAKE_WRAPPER4(ssize_t, send, int, sockfd, void *, buf, size_t, len, int, flags);
MAKE_WRAPPER5(int, setsockopt, int, sockfd, int, level, int, optname, const void *, optval, socklen_t, optlen);

sem_t *create_semaphore()
{
    sem_t *sem = sem_open(semaphore_name, O_CREAT, S_IRWXU, 0);
    if(sem == SEM_FAILED)
    {
        perror("sem_open() failed");
    }

    // increment semaphore name
    {
        for(uint32_t i = 0; i < ArrayCount(semaphore_name); i++)
        {
            if(semaphore_name[i] < 'z')
            {
                semaphore_name[i]++;
                break;
            }
        }
    }

    return sem;
}

/* utilities */
//{

uint32_t min(int x, int y)
{
    uint32_t result = (x < y) ? x : y;
    return result;
}

uint16_t get16be(uint8_t *src)
{
    uint16_t result = ((src[0] << 8) |
                       (src[1] << 0));
    return result;
}

uint64_t get64be(uint8_t *src)
{
    uint16_t result = (((uint64_t)src[0] << 56) |
                       ((uint64_t)src[1] << 48) |
                       ((uint64_t)src[2] << 40) |
                       ((uint64_t)src[3] << 32) |
                       ((uint64_t)src[4] << 24) |
                       ((uint64_t)src[5] << 16) |
                       ((uint64_t)src[6] << 8 ) |
                       ((uint64_t)src[7] << 0 ));
    return result;
}

void put16be(uint8_t *dest, uint16_t src)
{
    dest[0] = ((src & 0xff00) >> 8);
    dest[1] = ((src & 0x00ff) >> 0);
}

void put64be(uint8_t *dest, uint64_t src)
{
    dest[0] = ((src & 0xff00000000000000) >> 56);
    dest[1] = ((src & 0x00ff000000000000) >> 48);
    dest[2] = ((src & 0x0000ff0000000000) >> 40);
    dest[3] = ((src & 0x000000ff00000000) >> 32);
    dest[4] = ((src & 0x00000000ff000000) >> 24);
    dest[5] = ((src & 0x0000000000ff0000) >> 16);
    dest[6] = ((src & 0x000000000000ff00) >>  8);
    dest[7] = ((src & 0x00000000000000ff) >>  0);
}

int load_file(const char *filename, char **buffer_out, int *filesize_out)
{
    FILE *file_fp = fopen(filename, "rb");
    if(file_fp == NULL)
    {
        perror("fopen() failed");
        return false;
    }

    int filesize;
    {
        fseek(file_fp, 0, SEEK_END);
        filesize = ftell(file_fp);
        rewind(file_fp);
    }

    char *file_contents = (char *)malloc(filesize);
    if(file_contents == NULL)
    {
        perror("malloc() failed");
        return false;
    }

    int bytes_read = fread(file_contents, 1, filesize, file_fp);
    if(bytes_read != filesize)
    {
        printf("error reading webpage: expected: %d, actual: %d\n", bytes_read, filesize);
        return false;
    }

    fclose(file_fp);

    {
        *buffer_out = file_contents;
        *filesize_out = filesize;
    }

    return true;
}

uint32_t left_rotate(uint32_t value, int rots)
{
    // construct mask
    uint32_t mask;
    {
        mask = 0x80000000;
        for(int i = 1; i < rots; i++)
        {
            mask = ((mask >> 1) | 0x80000000);
        }
    }

    uint32_t shifted_off_bits = ((value & mask) >> (32 - rots));
    uint32_t rotated_value = ((value << rots) | shifted_off_bits);
    return rotated_value;
}

char to_base64(int value)
{
    char result;
    if(value <= 25)
    {
        result = 'A' + value;
    }
    else if(value <= 51)
    {
        result = 'a' + (value - 26);
    }
    else if(value <= 61)
    {
        result = '0' + (value - 52);
    }
    else if(value == 62)
    {
        result = '+';
    }
    else if(value == 63)
    {
        result = '/';
    }
    else
    {
        result = -1;
    }
    return result;
}

/* From pseudo code on Wikipedia. Returns sha1 hash as a base64 websocket_message string (28 bytes). */
bool sha1(char *string, char *dest)
{
    if(strlen(string) > 1024)
    {
        return false;
    }

    char message[1024];
    strcpy(message, string);

    uint32_t message_length = strlen(message);
    const uint64_t ml = 8*message_length; // message length in bits

    // magic variables
    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;

    // pre processing:
    {
        // append 0x80 to message
        {
            message[message_length++] = 0x80;
        }

        // append 0 bits until (ml % 512) == 448
        {
            // compute number of bits to append
            int num_appends_bits;
            {
                // we add 8 since we added the 0x80
                int mod = (ml + 8) & 511;
                if(mod == 448)
                {
                    num_appends_bits = 0;
                }
                else if(mod < 448)
                {
                    num_appends_bits = 448 - mod;
                }
                else//if(mod > 448)
                {
                    num_appends_bits = (512 - mod) + 448;
                }

                assert((num_appends_bits & 0x07) == 0);
            }

            // append 0 bytes
            {
                int num_appends = (num_appends_bits >> 3);
                for(int i = 0; i < num_appends; i++)
                {
                    message[message_length++] = 0x00;
                }
            }
        }

        // append message length
        {
            uint64_t mask = (((uint64_t)0xff) << 56);
            for(int i = 0; i < 8; i++)
            {
                message[message_length++] = ((ml & mask) >> (56 - 8*i));
                mask >>= 8;
            }
        }
    }

    // process message in successive 512-bit chunks
    uint32_t num_chunks = (message_length/64);
    for(uint32_t i = 0; i < num_chunks; i++)
    {
        uint32_t w[80];
        for(int j = 0; j < 16; j++)
        {
            w[j] = ((message[64*i + 4*j + 0] << 24) |
                    (message[64*i + 4*j + 1] << 16) |
                    (message[64*i + 4*j + 2] <<  8) |
                    (message[64*i + 4*j + 3] <<  0));
        }

        for(uint32_t j = 16; j < ArrayCount(w); j++)
        {
            w[j] = (w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16]);
            w[j] = left_rotate(w[j], 1);
        }

        // initialize hash value for this chunk:
        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;

        uint32_t f;
        uint32_t k;

        // main loop:
        for(uint32_t j = 0; j < ArrayCount(w); j++)
        {
            if(j < 20)
            {
                f = ((b & c) | ((~b) & d));
                k = 0x5A827999;
            }
            else if(j < 40)
            {
                f = (b ^ c ^ d);
                k = 0x6ED9EBA1;
            }
            else if(j < 60)
            {
                f = ((b & c) | (b & d) | (c & d));
                k = 0x8F1BBCDC;
            }
            else if(j < 80)
            {
                f = (b ^ c ^ d);
                k = 0xCA62C1D6;
            }

            int temp = (left_rotate(a, 5) + f + e + k + w[j]);
            e = d;
            d = c;
            c = left_rotate(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    uint8_t final_hash[20];
    {
        final_hash[0]  = ((h0 & 0xff000000) >> 24);
        final_hash[1]  = ((h0 & 0x00ff0000) >> 16);
        final_hash[2]  = ((h0 & 0x0000ff00) >> 8 );
        final_hash[3]  = ((h0 & 0x000000ff) >> 0 );

        final_hash[4]  = ((h1 & 0xff000000) >> 24);
        final_hash[5]  = ((h1 & 0x00ff0000) >> 16);
        final_hash[6]  = ((h1 & 0x0000ff00) >> 8 );
        final_hash[7]  = ((h1 & 0x000000ff) >> 0 );

        final_hash[8]  = ((h2 & 0xff000000) >> 24);
        final_hash[9]  = ((h2 & 0x00ff0000) >> 16);
        final_hash[10] = ((h2 & 0x0000ff00) >> 8 );
        final_hash[11] = ((h2 & 0x000000ff) >> 0 );

        final_hash[12] = ((h3 & 0xff000000) >> 24);
        final_hash[13] = ((h3 & 0x00ff0000) >> 16);
        final_hash[14] = ((h3 & 0x0000ff00) >> 8 );
        final_hash[15] = ((h3 & 0x000000ff) >> 0 );

        final_hash[16] = ((h4 & 0xff000000) >> 24);
        final_hash[17] = ((h4 & 0x00ff0000) >> 16);
        final_hash[18] = ((h4 & 0x0000ff00) >> 8 );
        final_hash[19] = ((h4 & 0x000000ff) >> 0 );
    }

    // convert to base64
    {
        int i, dest_idx;
        for(i = 0, dest_idx = 0; i < 18; i += 3, dest_idx += 4)
        {
            uint32_t three_set = ((final_hash[i + 0] << 16) | 
                                  (final_hash[i + 1] <<  8) | 
                                  (final_hash[i + 2] <<  0));

            uint8_t first =  ((three_set >> 18) & 0x3f);
            uint8_t second = ((three_set >> 12) & 0x3f);
            uint8_t third =  ((three_set >>  6) & 0x3f);
            uint8_t fourth = ((three_set >>  0) & 0x3f);

            dest[dest_idx + 0] = to_base64(first);
            dest[dest_idx + 1] = to_base64(second);
            dest[dest_idx + 2] = to_base64(third);
            dest[dest_idx + 3] = to_base64(fourth);
        }

        assert(i == 18);

        // handle remainder
        {
            uint32_t three_set = ((final_hash[i + 0] << 16) | 
                                  (final_hash[i + 1] <<  8));

            uint8_t first =  ((three_set >> 18) & 0x3f);
            uint8_t second = ((three_set >> 12) & 0x3f);
            uint8_t third =  ((three_set >>  6) & 0x3f);

            dest[dest_idx + 0] = to_base64(first);
            dest[dest_idx + 1] = to_base64(second);
            dest[dest_idx + 2] = to_base64(third);
            dest[dest_idx + 3] = '=';
        }

        // null terminate
        dest[dest_idx + 4] = 0;
    }

    return true;
}
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

    int sock_fd = socket_(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if(sock_fd == -1)
    {
        return -1;
    }

    int yes = 1;
    if(setsockopt_(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
        return -1;
    }

    if(bind_(sock_fd, servinfo->ai_addr, servinfo->ai_addrlen) == -1)
    {
        return -1;
    }

	freeaddrinfo(servinfo); // all done with this structure

	if(listen_(sock_fd, BACKLOG) == -1)
    {
        return -1;
	}

    return sock_fd;
}

int get_client(int sock_fd, const char *name = NULL)
{
    sockaddr_storage their_addr;
    socklen_t sin_size = sizeof(their_addr);
    int client_fd = accept_(sock_fd, (sockaddr *)&their_addr, &sin_size);
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

    sem_t *messages_semaphore;

    uint8_t *head;
    uint8_t *tail;
    uint8_t *end;
    uint8_t buffer[KILOBYTES(16)];
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

bool init_websocket(WebSocket *socket, int fd)
{
    socket->fd = fd;
    socket->head = socket->buffer;
    socket->tail = socket->buffer;
    socket->end = (socket->buffer + sizeof(socket->buffer));

    socket->messages_semaphore = create_semaphore();
    if(socket->messages_semaphore == SEM_FAILED)
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

    if(!init_websocket(socket, socket_fd))
    {
        return false;
    }

    return true;
}

uint8_t *get_message_insertion_ptr(WebSocket *socket)
{
    uint8_t *tail = socket->tail;

    // will the length overflow?
    if((tail + sizeof(uint32_t)) > socket->end)
    {
        tail = socket->buffer;
    }

    // are we empty, or won't overflow?
    if(socket->tail == socket->head ||
       ((tail + sizeof(uint32_t)) <= socket->head))
    {
        return tail;
    }

    return NULL;
}

uint8_t *get_message_extraction_ptr(WebSocket *socket)
{
    uint8_t *head = socket->head;

    // will the length overflow?
    if((head + sizeof(uint32_t)) > socket->end)
    {
        head = socket->buffer;
    }

    return head;
}

int add_message(WebSocket *socket, uint8_t *message, const uint32_t message_length)
{
    uint8_t *message_length_ptr = get_message_insertion_ptr(socket);
    if(message_length_ptr != NULL)
    {
        uint32_t actual_message_length = 0; // the length of the payload put in the socket's message buffer
        socket->tail = (message_length_ptr + sizeof(uint32_t));

        // copy the payload
        {
            uint32_t message_left = message_length;

            if(socket->tail > socket->head)
            {
                uint32_t bytes_to_end = (socket->end - socket->tail);
                uint32_t bytes_to_copy = min(message_left, bytes_to_end);
                for(uint32_t i = 0; i < bytes_to_copy; i++)
                {
                    *socket->tail++ = *message++;
                }
                message_left -= bytes_to_copy;
                assert(message_left >= 0);

                if(message_left > 0)
                {
                    socket->tail = socket->buffer;
                }
                else
                {
                    // we're done!
                    socket->tail += message_length;
                }
            }

            if(message_left > 0)
            {
                uint32_t bytes_left = (socket->head - socket->tail);
                uint32_t bytes_to_copy = min(message_left, bytes_left);
                for(uint32_t i = 0; i < bytes_to_copy; i++)
                {
                    *socket->tail++ = *message++;
                }
                message_left -= bytes_to_copy;
            }

            actual_message_length = (message_length - message_left);
        }

        *(uint32_t *)message_length_ptr = actual_message_length;

        sem_post_(socket->messages_semaphore);

        return actual_message_length;;
    }
    else
    {
        return -1;
    }
}

int recv(WebSocket *socket, uint8_t *dest)
{
    if(sem_wait_(socket->messages_semaphore) == -1)
    {
        return -1;
    }

    uint8_t *message_length_ptr = get_message_extraction_ptr(socket);
    const uint32_t message_length = *(uint32_t *)message_length_ptr;
    uint32_t message_left = message_length;
    socket->head = (message_length_ptr + sizeof(uint32_t));

    if(socket->head > socket->tail)
    {
        uint32_t bytes_to_end = (socket->end - socket->head);
        memcpy(dest, socket->head, bytes_to_end);
        dest += bytes_to_end;
        message_left -= bytes_to_end;
        socket->head = socket->buffer;
    }

    uint32_t bytes_left = (socket->tail - socket->head);
    uint32_t bytes_to_copy = min(message_left, bytes_left);
    memcpy(dest, socket->head, bytes_to_copy);
    message_left -= bytes_to_copy;
    socket->head += bytes_to_copy;

    uint32_t message_copied = (message_length - message_left);
    return message_copied;
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

    if(send_(socket_fd, frame, frame_length, 0) == -1)
    {
        return false;
    }

    return true;
}

void *background_websocket_client_thread_entry(void *thread_data);
bool get_client(WebSocket *socket, WebSocket *client_socket)
{
    int socket_fd = socket->fd;

    int client_fd = get_client(socket_fd);
    if(client_fd == -1)
    {
        return -1;
    }

    char client_handshake[4096];
    int client_handshake_length = 0;
    {
        int client_handshake_length = recv_(client_fd, client_handshake, sizeof(client_handshake), 0);
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

    if(send_(client_fd, client_handshake_reply, strlen(client_handshake_reply), 0) == -1) 
    {
        return -1;
    }

    init_websocket(client_socket, client_fd);

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

    if(send_(socket_fd, frame, frame_length, 0) == -1)
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
        if(recv_(socket_fd, frame, sizeof(frame), 0) == -1)
        {
            return false;
        }

        if((frame[0] & 0x0f) == 0x08)
        {
            break;
        }
    }

    close_(socket_fd);

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
        int frame_length = recv_(socket_fd, (char *)raw_frame, sizeof(raw_frame), 0);
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

        if(add_message(socket, frame.payload, frame.payload_length) != (int)frame.payload_length)
        {
            printf("failed to add message\n");
        }
    }

    return 0;
}
//}

void *client_websocket_thread_entry(void *thread_data)
{
    WebSocket *socket = (WebSocket *)thread_data;

#if 1
    // perform basic test
    {
        uint8_t test_message[256];
        int recv_length = recv(socket, test_message);
        if(recv_length == -1)
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
        pthread_create(&thread_pool[thread_pool_size++], NULL, client_websocket_thread_entry, &clients[num_clients++]);
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

        if(send_(client_fd, webpage, webpage_size, 0) == -1) 
        {
            exit(1);
        }
        close_(client_fd);
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
