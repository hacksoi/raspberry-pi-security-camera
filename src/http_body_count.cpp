#include "ns_file.h"

#include <stdio.h>


int 
main()
{
    printf("hello\n");

    NsFile file;
    ns_file_open(&file, "index.html");
    char buf[1024];
    int size = ns_file_load(&file, buf, sizeof(buf));

    int start_idx = 0;
    for(int i = 0;; i++)
    {
        if(buf[i] == '<' && buf[i+1] == '!')
        {
            start_idx = i;
            break;
        }
    }

    printf("total length: %d, body length = %d\n", size, size - start_idx);
}
