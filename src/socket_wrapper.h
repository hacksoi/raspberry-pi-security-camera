#ifndef SOCKET_WRAPPER_H
#define SOCKET_WRAPPER_H

#include "common.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define TIMED_OUT -2

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if(sa->sa_family == AF_INET)
    {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int create_socket(const char *port, int backlog = 10)
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
        DEBUG_PRINT_INFO();
		return -1;
	}

    int sock_fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if(sock_fd == -1)
    {
        DEBUG_PRINT_INFO();
        return -1;
    }

    int yes = 1;
    if(setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
        DEBUG_PRINT_INFO();
        return -1;
    }

    if(bind(sock_fd, servinfo->ai_addr, servinfo->ai_addrlen) == -1)
    {
        DEBUG_PRINT_INFO();
        return -1;
    }

	freeaddrinfo(servinfo); // all done with this structure

	if(listen(sock_fd, backlog) == -1)
    {
        DEBUG_PRINT_INFO();
        return -1;
	}

    return sock_fd;
}

int get_client(int sock_fd, uint32_t timeout_millis = 0, const char *name = NULL)
{
    if(timeout_millis > 0)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock_fd, &rfds);

        timeval tv = {};
        tv.tv_usec = 1000*timeout_millis;

        int select_result = select((sock_fd + 1), &rfds, NULL, NULL, &tv);
        if(select_result == -1)
        {
            DEBUG_PRINT_INFO();
            return -1;
        }

        if(select_result == 0)
        {
            return -2;
        }
    }

    sockaddr_storage their_addr;
    socklen_t sin_size = sizeof(their_addr);
    int client_fd = accept(sock_fd, (sockaddr *)&their_addr, &sin_size);
    if(client_fd == -1)
    {
        DEBUG_PRINT_INFO();
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

#endif
