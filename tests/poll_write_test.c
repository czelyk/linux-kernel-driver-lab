#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>

#define FIFO_SIZE 4096

#define AHMET_IOCTL_MAGIC 'A'
#define AHMET_CLEAR_BUFFER _IO(AHMET_IOCTL_MAGIC, 1)

int main(void)
{
    int fd;
    int ret;
    char *buffer;
    struct pollfd pfd;

    fd = open("/dev/ahmet0", O_RDWR | O_NONBLOCK);

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

    ret = write(fd, buffer, FIFO_SIZE);

    if (ret < 0)
    {
        perror("write");
        free(buffer);
        close(fd);
        return 1;
    }

    printf("FIFO tamamen dolduruldu: %d bytes\n", ret);

    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;

    printf("FIFO'da boş alan bekleniyor...\n");
    fflush(stdout);

    ret = poll(&pfd, 1, -1);

    if (ret < 0)
    {
        perror("poll");
        free(buffer);
        close(fd);
        return 1;
    }

    if (pfd.revents & POLLOUT)
    {
        printf("FIFO artık yazılabilir durumda\n");
    }

    if (pfd.revents & POLLERR)
    {
        printf("Cihaz hata bildirdi\n");
    }

    free(buffer);
    close(fd);

    return 0;
}