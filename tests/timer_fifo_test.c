#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

#define TIMER_MESSAGE_SIZE 12
#define EVENT_COUNT 5

int main(void)
{
    int fd;
    int poll_result;
    int event_number;
    char buffer[TIMER_MESSAGE_SIZE + 1];
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

    for (event_number = 1;
         event_number <= EVENT_COUNT;
         event_number++)
    {
        pfd.revents = 0;

        printf("%d. timer olayı bekleniyor...\n",
               event_number);
        fflush(stdout);

        poll_result = poll(&pfd, 1, -1);

        if (poll_result < 0)
        {
            if (errno == EINTR)
            {
                event_number--;
                continue;
            }

            perror("poll");
            close(fd);
            return 1;
        }

        if (pfd.revents & POLLIN)
        {
            bytes_read = read(fd,
                              buffer,
                              TIMER_MESSAGE_SIZE);

            if (bytes_read < 0)
            {
                perror("read");
                close(fd);
                return 1;
            }

            buffer[bytes_read] = '\0';

            printf("%d. okuma (%zd byte): %s",
                   event_number,
                   bytes_read,
                   buffer);

            if (bytes_read == 0 ||
                buffer[bytes_read - 1] != '\n')
            {
                putchar('\n');
            }
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