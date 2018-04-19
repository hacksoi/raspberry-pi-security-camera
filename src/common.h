#ifndef COMMON_H
#define COMMON_H

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* preprocessor directives */
//{

#define ArrayCount(array) (sizeof(array)/sizeof(array[0]))
#define KILOBYTES(x) (x*1024)
#define MEGABYTES(x) (x*1024*1024)
//}

/* misc functions */
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

/* From pseudo code on Wikipedia. Returns sha1 hash as a base64 string (28 bytes). */
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

/* files */
//{
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
//}

#endif
