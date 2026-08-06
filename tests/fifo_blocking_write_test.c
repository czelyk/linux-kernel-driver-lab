#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

#define AHMET_IOCTL_MAGIC 'A'
#define AHMET_CLEAR_BUFFER _IO(AHMET_IOCTL_MAGIC, 1)

#define FIFO_SIZE 4096

int main(void)
{
    int fd;
    char *buffer;
    ssize_t result;
    pid_t pid;

    fd = open("/dev/ahmet0", O_RDWR);

    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    if (ioctl(fd, AHMET_CLEAR_BUFFER) < 0)
    {
        perror("ioctl CLEAR_BUFFER");
        close(fd);
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

    printf("FIFO filled with %zd bytes\n", result);
    fflush(stdout);

    pid = fork();

    if (pid < 0)
    {
        perror("fork");
        free(buffer);
        close(fd);
        return 1;
    }

    if (pid == 0)
    {
        char read_buffer[16];

        sleep(2);

        result = read(fd, read_buffer, sizeof(read_buffer));

        if (result < 0)
        {
            perror("child read");
            close(fd);
            _exit(1);
        }

        printf("Child read %zd bytes\n", result);
        fflush(stdout);

        close(fd);
        _exit(0);
    }

    printf("Parent is trying to write one more byte...\n");
    fflush(stdout);

    result = write(fd, "X", 1);

    if (result < 0)
    {
        perror("parent write");
    }
    else
    {
        printf("Parent write completed: %zd byte\n", result);
    }

    wait(NULL);

    free(buffer);
    close(fd);

    return 0;
}