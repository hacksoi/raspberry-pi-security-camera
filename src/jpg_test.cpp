#include <stdint.h>
#include <unistd.h>
#include <stdio.h>

enum State
{
    LOOKING_IMAGE,
    READING_IMAGE,
};

uint8_t buf[20000];

int main()
{
    FILE *file = fopen("test_image.jpg", "wb");
    if(file == NULL)
    {
        perror("fopen() failed");
        return 1;
    }

    State cur_state = LOOKING_IMAGE;

    uint32_t frames;
    uint16_t two = 0;
    uint32_t size;
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

                    buf[size++] = 0xff;
                    buf[size++] = 0xd8;
                }
            } break;

            case READING_IMAGE:
            {
                if(size < sizeof(buf))
                {
                    buf[size++] = c;
                }

                if(two == 0xffd9)
                {
                    printf("end of image detected: %d bytes\n", size);

                    if(++frames > 15)
                    {
                        if(size < sizeof(buf))
                        {
                            if(fwrite(buf, 1, size, file) != size)
                            {
                                perror("fwrite() failed");
                                return 1;
                            }

                            return 0;
                        }
                    }

                    // set up for next state
                    cur_state = LOOKING_IMAGE;
                }
            } break;
        }

        two <<= 8;
        two |= c;
    }
}
