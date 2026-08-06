#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

int main(void)
{
    int fd;
    const char *message = "ABCDEF";
    char buffer[4];
    ssize_t bytes_read;

    fd = open("/dev/ahmet0", O_RDWR | O_NONBLOCK);

    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    if (write(fd, message, strlen(message)) < 0)
    {
    perror("write");
    close(fd);
    return 1;
    }
    memset(buffer, 0, sizeof(buffer));

    bytes_read = read(fd, buffer, 3);

    if (bytes_read < 0)
    {
        perror("first read");
        close(fd);
        return 1;
    }

    buffer[bytes_read] = '\0';

    printf("First read: %s\n", buffer);

    memset(buffer, 0, sizeof(buffer));

    bytes_read = read(fd, buffer, 3);

    if (bytes_read < 0)
    {
        perror("second read");
        close(fd);
        return 1;
    }

    buffer[bytes_read] = '\0';

    printf("Second read: %s\n", buffer);

    memset(buffer, 0, sizeof(buffer));

    bytes_read = read(fd, buffer, 3);

    if(bytes_read < 0)
        perror("Third read");

    else 
    {
        buffer[bytes_read] = '\0';
        printf("Third readÇ %s\n", buffer);
    }

    close(fd);
    return 0;
}