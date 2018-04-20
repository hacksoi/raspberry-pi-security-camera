#include "common.h"
#include "message_buffer.h"

#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define MESSAGE_BUFFER_SIZE KILOBYTES(16)

#define MAX_MESSAGES 100
int message_sizes[MAX_MESSAGES];
char messages[(3*MESSAGE_BUFFER_SIZE*MAX_MESSAGES)/4];
char *messages_end = (messages + sizeof(messages));
int num_messages;

void *add_thread_entry(void *thread_data)
{
    MessageBuffer *message_buffer = (MessageBuffer *)thread_data;

    char *cur_messages_ptr = messages;
    for(int i = 0; i < num_messages; i++)
    {
        uint32_t message_size = *(uint32_t *)cur_messages_ptr;

        printf("add thread: adding size = %d\n", message_size);
        while(!check_has_room(message_buffer, message_size))
        {
            if(pthread_yield())
            {
                DEBUG_PRINT_INFO();
                exit(1);
            }
        }

        int add_size = add(message_buffer, (cur_messages_ptr + sizeof(uint32_t)), message_size);
        printf("add thread: added size = %d\n", message_size);
        if(add_size == -1)
        {
            DEBUG_PRINT_INFO();
            exit(1);
        }

        cur_messages_ptr += (sizeof(uint32_t) + message_size);
    }

    return 0;
}

void *get_thread_entry(void *thread_data)
{
    printf("get_thread_entry()\n");

    MessageBuffer *message_buffer = (MessageBuffer *)thread_data;

    char dest[MESSAGE_BUFFER_SIZE];
    char *cur_messages_ptr = messages;
    for(int i = 0; i < num_messages; i++)
    {
        uint32_t message_size = *(uint32_t *)cur_messages_ptr;
        cur_messages_ptr += sizeof(uint32_t);

        if(message_size == 0)
        {
            continue;
        }

        printf("get thread: getting size = %d\n", message_size);
        int get_size = get(message_buffer, dest, sizeof(dest));
        printf("get thread: got size = %d\n", message_size);
        if(get_size == -1)
        {
            DEBUG_PRINT_INFO();
            exit(1);
        }

        if(get_size != (int)message_size)
        {
            DEBUG_PRINT_INFO();
            printf("get_size: %d, message_size: %d\n", get_size, message_size);
            exit(1);
        }

        // check message
        for(uint32_t j = 0; j < message_size; j++)
        {
            if(dest[j] != cur_messages_ptr[j])
            {
                printf("byte: %d, dest[j]: %d, cur_messages_ptr: %d\n", j, dest[j], cur_messages_ptr[j]);
                DEBUG_PRINT_INFO();
                exit(1);
            }
        }

        cur_messages_ptr += message_size;
    }

    printf("we're done!!!\n");

    return 0;
}

int main()
{
    printf("\nrunning message buffer test...\n\n");

#if 1
    uint32_t seed = time(NULL);
#else
    uint32_t seed = 1524196387;
#endif
    printf("srand seed: %d\n", seed);
    srand(seed);

    MessageBuffer message_buffer;
    init(&message_buffer, MESSAGE_BUFFER_SIZE);

    // fill out the sizes
    {
        for(uint32_t i = 0; i < ArrayCount(message_sizes); i++)
        {
            // a message can't equal the size of the buffer (- 1) we can skip 3 bytes at the end and 4 bytes for the size
            message_sizes[i] = (rand() % (message_buffer.size - 1 - (sizeof(uint32_t) - 1) - sizeof(uint32_t)));
        }

        int rand_idx0 = (rand() % ArrayCount(message_sizes));
        message_sizes[rand_idx0] = 0;

        int rand_idx1;
        do
        {
            rand_idx1 = (rand() % ArrayCount(message_sizes));
        } while(rand_idx0 != rand_idx1);
        message_sizes[rand_idx1] = 1;
    }

    // fill out the messages
    {
        char *cur_messages_ptr = messages;
        for(num_messages = 0; num_messages < MAX_MESSAGES; num_messages++)
        {
            int message_size = message_sizes[num_messages];
            if((cur_messages_ptr + sizeof(uint32_t) + message_size) > messages_end)
            {
                break;
            }

            *(uint32_t *)cur_messages_ptr = message_size;
            cur_messages_ptr += sizeof(uint32_t);

            // fill out message
            for(int i = 0; i < message_size; i++)
            {
                *cur_messages_ptr++ = (rand() % sizeof(char));
            }
        }
    }

    pthread_t add_thread;
    pthread_create(&add_thread, NULL, add_thread_entry, &message_buffer);

    pthread_t get_thread;
    pthread_create(&get_thread, NULL, get_thread_entry, &message_buffer);

    // keeps other threads alive after this main thread exits
    pthread_exit(NULL);

    return 0;
}
