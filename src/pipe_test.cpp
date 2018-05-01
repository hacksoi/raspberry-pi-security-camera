#include <unistd.h>
#include <stdio.h>

/* Spawns a process and sets its stdout to point to our stdin. */
int
fork_process(const char *filename, char *const *argv)
{
    int status;

    // create pipe
    int fd[2];
    status = pipe(fd);
    if(status == -1)
    {
        perror("pipe() failed");
        return 1;
    }

    int pipe_read = fd[0];
    int pipe_write = fd[1];

    // fork the fuck out of the child
    int fork_result = fork();
    if(fork_result < 0)
    {
        perror("fork() failed");
        return 1;
    }

    // are we the parent?
    if(fork_result > 0)
    {
        status = close(pipe_write);
        if(status == -1)
        {
            perror("closed() failed");
            return 1;
        }

        status = dup2(pipe_read, STDIN_FILENO);
        if(status == -1)
        {
            perror("dup2() failed");
            return 1;
        }
    }
    else
    {
        status = close(pipe_read);
        if(status == -1)
        {
            perror("closed() failed");
            return 1;
        }

        // set child's stdout to pipe
        status = dup2(pipe_write, STDOUT_FILENO);
        if(status == -1)
        {
            perror("dup2() failed");
            return 1;
        }

        char *const env[] = {NULL};

        // exec child
        status = execve(filename, argv, env);
        if(status == -1)
        {
            perror("execve() failed");
            return 1;
        }
    }

    return 0;
}

int 
main()
{
    int status;

    char *const argv[] = {(char *)"pipe_test2", NULL};
    status = fork_process("./pipe_test2", argv);
    if(status != 0)
    {
        return 1;
    }
}
