#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#define FIFO_SIZE 4096

int main(void)
{
    int fd;
    char *buffer;
    ssize_t result;

    fd = open("/dev/ahmet0", O_WRONLY | O_NONBLOCK);

    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    buffer = malloc(FIFO_SIZE);

    if (!buffer)
    {
        perror("malloc");
        close(fd);
        return 1;
    }

    memset(buffer, 'A', FIFO_SIZE);

    result = write(fd, buffer, FIFO_SIZE);

    if (result < 0)
    {
        perror("first write");
        free(buffer);
        close(fd);
        return 1;
    }

    printf("First write: %zd bytes\n", result);

    result = write(fd, "X", 1);

    if (result < 0)
    {
        perror("Second write");
    }
    else
    {
        printf("Second write: %zd bytes\n", result);
    }

    free(buffer);
    close(fd);

    return 0;
}