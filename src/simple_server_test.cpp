#include "ns_socket.h"
#include "ns_file.h"

#include <stdio.h>


int 
main()
{
    printf("hello world\n");

    ns_sockets_startup();

    NsSocket socket;
    ns_socket_listen(&socket, "40873");

    NsSocket peer;
    ns_socket_get_client(&socket, &peer);

    printf("receiving...\n");

    char buf[1024];
    int ms = ns_socket_receive(&peer, buf, sizeof(buf));

    for(int i = 0; i < ms; i++)
    {
        putchar(buf[i]);
    }

    printf("sending...\n");

    NsFile file;
    ns_file_open(&file, (char *)"index.html");
    int res_size = ns_file_load(&file, buf, sizeof(buf));
    ns_file_close(&file);
    ns_socket_send(&peer, buf, res_size);

    printf("sent!\n");

    printf("receiving...\n");

    ms = ns_socket_receive(&peer, buf, sizeof(buf));

    for(int i = 0; i < ms; i++)
    {
        putchar(buf[i]);
    }

    printf("done!...\n");

    return 0;
}
