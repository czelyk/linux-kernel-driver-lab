#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

#define EVENT_COUNT 10

int main(void)
{
    int fd;
    int poll_result;
    int event_count = 0;
    char ch;
    ssize_t bytes_read;
    struct pollfd pfd;

    fd = open("/dev/ahmet_fifo0", O_RDONLY);

    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    pfd.fd = fd;
    pfd.events = POLLIN;

    while (event_count < EVENT_COUNT)
    {
        pfd.revents = 0;

        poll_result = poll(&pfd, 1, -1);

        if (poll_result < 0)
        {
            if (errno == EINTR)
                continue;

            perror("poll");
            close(fd);
            return 1;
        }

        if (pfd.revents & POLLIN)
        {
            /*
             * FIFO byte akışı olduğu için birer byte okuyup
             * '\n' karakterine kadar aynı mesajı yazdırıyoruz.
             */
            bytes_read = read(fd, &ch, 1);

            if (bytes_read < 0)
            {
                perror("read");
                close(fd);
                return 1;
            }

            if (bytes_read == 0)
                continue;

            putchar(ch);
            fflush(stdout);

            if (ch == '\n')
                event_count++;
        }

        if (pfd.revents & POLLERR)
        {
            fprintf(stderr, "Cihaz POLLERR bildirdi\n");
            close(fd);
            return 1;
        }

        if (pfd.revents & POLLHUP)
        {
            fprintf(stderr, "Cihaz POLLHUP bildirdi\n");
            close(fd);
            return 1;
        }
    }

    close(fd);
    return 0;
}