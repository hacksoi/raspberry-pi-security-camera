#include "common.h"

#include <stdint.h>
#include <semaphore.h>
#include <string.h>

/* Thread-safe circular buffer */
struct MessageBuffer
{
    // NOTE:
    //  -an empty message buffer means (head == tail_
    //  -a full message buffer means (tail + 1 == head)
    //  -we define the end of the buffer to be the beginning. it's easier to just increment 
    //   the head or tail (when adding or removing a message) and just leave it at the end. 

    sem_t semaphore;

    uint8_t *head;
    uint8_t *tail;
    uint8_t *end;
    uint8_t buffer[KILOBYTES(16)];
};

bool init(MessageBuffer *message_buffer)
{
    message_buffer->end = (message_buffer->buffer + sizeof(message_buffer->buffer));
    message_buffer->head = message_buffer->end;
    message_buffer->tail = message_buffer->end;
    if(sem_init_(&message_buffer->semaphore, 0, 0) == -1)
    {
        return false;
    }
    return true;
}

/* Returns number of bytes of message added. */
int add(MessageBuffer *message_buffer, uint8_t *message, const uint32_t message_size)
{
    if(message_size > sizeof(message_buffer->buffer))
    {
        return -1;
    }

    const uint8_t *head = message_buffer->head;

    // do we have room?
    {
        uint32_t bytes_left = 0;
        if(message_buffer->tail >= head)
        {
            bytes_left += (message_buffer->end - message_buffer->tail);
        }
        bytes_left += (head - message_buffer->buffer - 1);

        if(message_size > bytes_left)
        {
            return -1;
        }
    }

    uint8_t *message_size_ptr = message_buffer->tail;
    uint8_t *message_ptr;
    {
        // does the size wrap?
        if((message_size_ptr + sizeof(uint32_t)) > message_buffer->end)
        {
            if(message_size_ptr < head)
            {
                // the head is between us and the end. if we wrapped, we'd go past it
                return -1;
            }
            else
            {
                message_size_ptr = message_buffer->buffer;
            }

            message_ptr = (message_size_ptr + sizeof(uint32_t));
        }
        else
        {
            message_ptr = (message_size_ptr + sizeof(uint32_t));

            // is the message at the very end?
            if(message_ptr == message_buffer->end)
            {
                message_ptr = message_buffer->buffer;
            }
            else
            {
                // we're done
            }
        }

        // will we go at or past the head?
        if((message_ptr + sizeof(uint8_t)) >= head)
        {
            return -1;
        }
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

    if(sem_post_(&message_buffer->semaphore) == -1)
    {
        return -1;
    }

    return message_size;
}

int get(MessageBuffer *message_buffer, uint8_t *dest, uint32_t dest_size)
{
    if(sem_wait_(&message_buffer->semaphore) == -1)
    {
        return -1;
    }

    const uint8_t *tail = message_buffer->tail;

    uint8_t *message_size_ptr = message_buffer->head;
    uint8_t *message_ptr;
    {
        // does the size wrap?
        if((message_size_ptr + sizeof(uint32_t)) > message_buffer->end)
        {
            message_size_ptr = message_buffer->buffer;
            message_ptr = (message_size_ptr + sizeof(uint32_t));
        }
        else
        {
            // is the message at the very end?
            message_ptr = (message_size_ptr + sizeof(uint32_t));
            if(message_ptr == message_buffer->end)
            {
                message_ptr = message_buffer->buffer;
            }
            else
            {
                // we're done
            }
        }
    }

    const uint32_t message_size = *(uint32_t *)message_size_ptr;

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
