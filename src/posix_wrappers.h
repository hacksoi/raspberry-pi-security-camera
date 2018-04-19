#if 0
#ifndef POSIX_WRAPPERS_H
#define POSIX_WRAPPERS_H

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <semaphore.h>

/* wrappers */
//{

#define MAKE_POSIX_WRAPPER1(return_value, name, t1, n1) \
    return_value name##_(t1 n1) \
    { \
        int result = name(n1);\
        if(result == -1) \
        { \
            perror(#name "() failed"); \
        } \
        return result; \
    }

#define MAKE_POSIX_WRAPPER2(return_value, name, t1, n1, t2, n2) \
    return_value name##_(t1 n1, t2 n2) \
    { \
        int result = name(n1, n2);\
        if(result == -1) \
        { \
            perror(#name "() failed"); \
        } \
        return result; \
    }

#define MAKE_POSIX_WRAPPER3(return_value, name, t1, n1, t2, n2, t3, n3) \
    return_value name##_(t1 n1, t2 n2, t3 n3) \
    { \
        int result = name(n1, n2, n3);\
        if(result == -1) \
        { \
            perror(#name "() failed"); \
        } \
        return result; \
    }

#define MAKE_POSIX_WRAPPER4(return_value, name, t1, n1, t2, n2, t3, n3, t4, n4) \
    return_value name##_(t1 n1, t2 n2, t3 n3, t4 n4) \
    { \
        int result = name(n1, n2, n3, n4);\
        if(result == -1) \
        { \
            perror(#name "() failed"); \
        } \
        return result; \
    }

#define MAKE_POSIX_WRAPPER5(return_value, name, t1, n1, t2, n2, t3, n3, t4, n4, t5, n5) \
    return_value name##_(t1 n1, t2 n2, t3 n3, t4 n4, t5 n5) \
    { \
        int result = name(n1, n2, n3, n4, n5);\
        if(result == -1) \
        { \
            perror(#name "() failed"); \
        } \
        return result; \
    }

MAKE_POSIX_WRAPPER1(int, sem_post, sem_t *, sem);
MAKE_POSIX_WRAPPER1(int, sem_wait, sem_t *, sem);
MAKE_POSIX_WRAPPER3(int, sem_init, sem_t *, sem, int, pshared, unsigned int, value);

MAKE_POSIX_WRAPPER3(int, socket, int, domain, int, type, int, protocol);
MAKE_POSIX_WRAPPER1(int, close, int, fd);
MAKE_POSIX_WRAPPER5(int, setsockopt, int, sockfd, int, level, int, optname, const void *, optval, socklen_t, optlen);
MAKE_POSIX_WRAPPER3(int, bind, int, sockfd, const struct sockaddr *, addr, socklen_t, addrlen);
MAKE_POSIX_WRAPPER2(int, listen, int, sockfd, int, backlog);
MAKE_POSIX_WRAPPER3(int, accept, int, sockfd, struct sockaddr *, addr, socklen_t *, addrlen);
MAKE_POSIX_WRAPPER4(ssize_t, recv, int, sockfd, void *, buf, size_t, len, int, flags);
MAKE_POSIX_WRAPPER4(ssize_t, send, int, sockfd, void *, buf, size_t, len, int, flags);

void *malloc_(size_t size)
{
    void *result = malloc(size);
    if(result == NULL)
    {
        perror();
    }
}
//}

#endif
#endif
