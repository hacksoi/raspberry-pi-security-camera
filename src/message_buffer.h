#ifndef MESSAGE_BUFFER_H
#define MESSAGE_BUFFER_H

#include "common.h"

#include <stdint.h>
#include <semaphore.h>
#include <string.h>

/* Thread-safe circular buffer. */
struct MessageBuffer
{
    // NOTE:
    //  -an empty message buffer means (head == tail_
    //  -a full message buffer means (tail + 1 == head)
    //  -we define the end of the buffer to be the beginning. it's easier to just increment 
    //   the head or tail (when adding or removing a message) and just leave it at the end. 

    sem_t read_write_semaphore;

    uint32_t size;
    uint8_t *buffer;
    uint8_t *end;
    uint8_t *head;
    uint8_t *tail;
};

void print(MessageBuffer *message_buffer)
{
    printf("message_buffer: start: %p, end: %p, head: %p, tail: %p\n",
           message_buffer->buffer, message_buffer->end, message_buffer->head, message_buffer->tail);
}

bool init(MessageBuffer *message_buffer, uint32_t size = KILOBYTES(4))
{
    message_buffer->size = size;
    message_buffer->buffer = (uint8_t *)malloc(size);
    if(message_buffer->buffer == NULL)
    {
        DEBUG_PRINT_INFO();
        return false;
    }
    message_buffer->end = (message_buffer->buffer + size);
    message_buffer->head = message_buffer->end;
    message_buffer->tail = message_buffer->end;

    if(sem_init(&message_buffer->read_write_semaphore, 0, 0) == -1)
    {
        DEBUG_PRINT_INFO();
        return false;
    }

    return true;
}

void destroy(MessageBuffer *message_buffer)
{
    free(message_buffer->buffer);
    sem_close(&message_buffer->read_write_semaphore);
}

bool is_empty(MessageBuffer *message_buffer)
{
    bool result = (message_buffer->head == message_buffer->tail);
    return result;
}

/* Returns whether there's enough room. We pass the head in case the MessageBuffer's
   head changes in between the call. */
bool get_message_insertion_pointers(MessageBuffer *message_buffer, const uint8_t *head, 
                                    const uint32_t message_size, 
                                    uint8_t **_message_size_ptr, uint8_t **_message_ptr)
{
    uint8_t *message_size_ptr = message_buffer->tail;
    {
        // does the size wrap?
        if((message_size_ptr + sizeof(uint32_t)) > message_buffer->end)
        {
            // would we go past the head?
            if(message_size_ptr < head)
            {
                DEBUG_PRINT_INFO();
                return false;
            }

            message_size_ptr = message_buffer->buffer;
        }
    }

    uint8_t *message_ptr = (message_size_ptr + sizeof(uint32_t));
    {
        if((message_size_ptr < head) && (message_ptr >= head))
        {
            DEBUG_PRINT_INFO();
            return false;
        } 

        if(message_ptr == message_buffer->end)
        {
            message_ptr = message_buffer->buffer;
        }
    }

    // do we have room?
    {
        uint32_t bytes_left = 0;
        {
            if(message_ptr > head)
            {
                // wrap around to beginning
                bytes_left += (message_buffer->end - message_ptr);

                bytes_left += (head - message_buffer->buffer - 1);
            }
            else
            {
                bytes_left += (head - message_ptr - 1); 
            }
        }

        if(message_size > bytes_left)
        {
            DEBUG_PRINT_INFO();
            return false;
        }
    }

    if((_message_size_ptr != NULL) && (_message_ptr != NULL))
    {
        *_message_size_ptr = message_size_ptr;
        *_message_ptr = message_ptr;
    }

    return true;
}

bool check_has_room(MessageBuffer *message_buffer, uint32_t message_size)
{
    bool has_room = get_message_insertion_pointers(message_buffer, message_buffer->head, 
                                                   message_size, NULL, NULL);
    return has_room;
}

/* Returns number of bytes of message added. */
int add(MessageBuffer *message_buffer, uint8_t *message, const uint32_t message_size)
{
    if(message_size == 0)
    {
        return 0;
    }

    if(message_size > message_buffer->size)
    {
        DEBUG_PRINT_INFO();
        return -1;
    }

    const uint8_t *head = message_buffer->head;

    uint8_t *message_size_ptr, *message_ptr;
    if(!get_message_insertion_pointers(message_buffer, head, 
                                       message_size,
                                       &message_size_ptr, &message_ptr))
    {
        return -1;
    }

    *(uint32_t *)message_size_ptr = message_size;

    // copy payload
    uint8_t *tail = message_ptr;
    {
        uint32_t message_left = message_size;

        if(tail > head)
        {
            uint32_t bytes_to_end = (message_buffer->end - tail);
            uint32_t bytes_to_copy = min(message_left, bytes_to_end);
            memcpy(tail, message, bytes_to_copy);
            message += bytes_to_copy;
            message_left -= bytes_to_copy;
            if(message_left > 0)
            {
                tail = message_buffer->buffer;
            }
            else
            {
                // we're done!
                tail += message_size;
            }
        }

        if(message_left > 0)
        {
            memcpy(tail, message, message_left);
            tail += message_left;
        }
    }
    message_buffer->tail = tail;

    if(sem_post(&message_buffer->read_write_semaphore) == -1)
    {
        DEBUG_PRINT_INFO();
        return -1;
    }

    return message_size;
}

int add(MessageBuffer *message_buffer, char *message, const uint32_t message_size)
{
    int result = add(message_buffer, (uint8_t *)message, message_size);
    return result;
}

int get(MessageBuffer *message_buffer, uint8_t *dest, uint32_t dest_size, bool is_blocking = true)
{
    if(is_blocking)
    {
        if(sem_wait(&message_buffer->read_write_semaphore) == -1)
        {
            DEBUG_PRINT_INFO();
            return -1;
        }
    }
    else if(is_empty(message_buffer))
    {
        DEBUG_PRINT_INFO();
        return -1;
    }

    const uint8_t *tail = message_buffer->tail;

    uint8_t *message_size_ptr = message_buffer->head;
    {
        // does the size wrap?
        if((message_size_ptr + sizeof(uint32_t)) > message_buffer->end)
        {
            message_size_ptr = message_buffer->buffer;
        }
    }

    uint8_t *message_ptr = (message_size_ptr + sizeof(uint32_t));
    {
        if(message_ptr == message_buffer->end)
        {
            message_ptr = message_buffer->buffer;
        }
    }

    const uint32_t message_size = *(uint32_t *)message_size_ptr;
    assert(message_size != 0);

    // copy payload
    uint8_t *head = message_ptr;
    {
        uint32_t message_left = message_size;
        uint32_t dest_left = dest_size;

        if(head > tail)
        {
            uint32_t bytes_to_end = (message_buffer->end - head);
            uint32_t bytes_to_copy = min(dest_left, min(message_left, bytes_to_end));
            memcpy(dest, head, bytes_to_copy);
            dest += bytes_to_copy;
            dest_left -= bytes_to_copy;
            message_left -= bytes_to_copy;

            // is there still more to copy?
            if((dest_left > 0) &&
               (message_left > 0))
            {
                head = message_buffer->buffer;
            }
            else
            {
                // we're done
                head += bytes_to_copy;
            }
        }

        // is dest large enough?
        if(dest_left >= message_left)
        {
            uint32_t bytes_to_copy = min(dest_left, message_left);
            memcpy(dest, head, bytes_to_copy);
            head += bytes_to_copy;
        }
        else
        {
            // where we'll put the shortened message size
            uint8_t *shortened_message_size_ptr = ((head + dest_left) - sizeof(uint32_t));

            // do we have enough room for the size in case we run out of dest?
            if(shortened_message_size_ptr >= message_buffer->buffer)
            {
                memcpy(dest, head, dest_left);
                message_left -= dest_left;
                head = shortened_message_size_ptr;
            }
            else
            {
                // note that at this point, the head must be pointed at the beginning of the buffer, meaning none of the
                // message has been read from the start of the buffer. if that wasn't the case, it'd be an issue
                // because, for example, if we were on the third byte, then jumped back to the end, then when we started
                // reading again, we'd reread the first two bytes.

                head = (message_buffer->end - sizeof(uint32_t));
            }

            *(uint32_t *)head = message_left;
        }
    }
    message_buffer->head = head;

    return message_size;
}

int get(MessageBuffer *message_buffer, char *dest, uint32_t dest_size, bool is_blocking = true)
{
    int result = get(message_buffer, (uint8_t *)dest, dest_size, is_blocking);
    return result;
}

#endif
