#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>

int main(void)
{
    int fd;
    int ret;
    char buffer[128];
    ssize_t bytes_read;
    struct pollfd pfd;

    fd = open("/dev/ahmet0", O_RDONLY);

    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    printf("FIFO verisi bekleniyor...\n");
    fflush(stdout);

    ret = poll(&pfd, 1, -1);

    if (ret < 0)
    {
        perror("poll");
        close(fd);
        return 1;
    }

    if (pfd.revents & POLLIN)
    {
        bytes_read = read(fd,
                          buffer,
                          sizeof(buffer) - 1);

        if (bytes_read < 0)
        {
            perror("read");
            close(fd);
            return 1;
        }

        buffer[bytes_read] = '\0';

        printf("FIFO'dan okunan: %s\n", buffer);
    }

    if (pfd.revents & POLLERR)
    {
        printf("Cihaz hata bildirdi\n");
    }

    if (pfd.revents & POLLHUP)
    {
        printf("Cihaz bağlantısı kapandı\n");
    }

    close(fd);

    return 0;
}