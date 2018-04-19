#include "message_buffer.h"

#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define MAX_MESSAGES 1000
int message_sizes[MAX_MESSAGES];
char messages[30*MAX_MESSAGES];
char *messages_end = (messages + sizeof(messages));
int num_messages;

void *add_thread_entry(void *thread_data)
{
    MessageBuffer *message_buffer = (MessageBuffer *)thread_data;

    char *cur_messages_ptr = messages;
    for(int i = 0; i < num_messages; i++)
    {
        uint32_t message_size = *(uint32_t *)cur_messages_ptr;
        printf("add thread: cur_size = %d\n", message_size);

        while(!check_has_room(message_buffer, message_size))
        {
            if(pthread_yield())
            {
                DEBUG_PRINT_INFO();
                exit(1);
            }
        }

        int add_size = add(message_buffer, (cur_messages_ptr + sizeof(uint32_t)), message_size);
        if(add_size == -1)
        {
            DEBUG_PRINT_INFO();
            exit(1);
        }

        cur_messages_ptr += (sizeof(uint32_t) + message_size);
    }

    return 0;
}

int main()
{
    printf("\nrunning message buffer test...\n\n");

    //uint32_t seed = time(NULL);
    uint32_t seed = 1524116982;
    printf("srand seed: %d\n", seed);
    srand(seed);

    MessageBuffer message_buffer;
    init(&message_buffer, 32);

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

    char dest[256];
    char *cur_messages_ptr = messages;
    for(int i = 0; i < num_messages; i++)
    {
        uint32_t message_size = *(uint32_t *)cur_messages_ptr;
        cur_messages_ptr += sizeof(uint32_t);

        if(message_size == 0)
        {
            continue;
        }

        int get_size = get(&message_buffer, dest, sizeof(dest));
        if(get_size == -1)
        {
            DEBUG_PRINT_INFO();
            return -1;
        }

        if(memcmp(dest, cur_messages_ptr, message_size))
        {
            DEBUG_PRINT_INFO();
            return -1;
        }

        cur_messages_ptr += message_size;
    }

    printf("\nfinished running message buffer test\n");

    return 0;
}
