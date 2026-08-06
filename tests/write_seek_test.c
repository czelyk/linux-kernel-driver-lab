#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

int main(void)
{
    int fd;
    const char *initial_data = "ABCDEFGHIJ";
    const char *replacement = "XYZ";

    char buffer[32];
    ssize_t bytes_read;

    fd = open("/dev/ahmet0", O_RDWR);

    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    if (write(fd, initial_data, strlen(initial_data)) < 0)
    {
        perror("initial write");
        close(fd);
        return 1;
    }

        if (lseek(fd, 4, SEEK_SET) < 0)
    {
        perror("lseek");
        close(fd);
        return 1;
    }

    if (write(fd, replacement, strlen(replacement)) < 0)
    {
        perror("replacement write");
        close(fd);
        return 1;
    }

        if (lseek(fd, 0, SEEK_SET) < 0)
    {
        perror("lseek start");
        close(fd);
        return 1;
    }

    memset(buffer, 0, sizeof(buffer));

    bytes_read = read(fd, buffer, sizeof(buffer) - 1);

    if (bytes_read < 0)
    {
        perror("read");
        close(fd);
        return 1;
    }

    buffer[bytes_read] = '\0';

    printf("Result: %s\n", buffer);

    close(fd);

    return 0;
}
