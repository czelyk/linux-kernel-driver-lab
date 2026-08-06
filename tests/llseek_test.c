#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

int main(void)
{
    int fd;
    const char *message = "ABCDEFGHIJ";
    char buffer[16];
    ssize_t bytes_read;
    off_t new_position;

    fd = open("/dev/ahmet0", O_RDWR);

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

    new_position = lseek(fd, 4, SEEK_SET);

    if (new_position < 0)
    {
        perror("lseek SEEK_SET");
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

    printf("New position: %lld\n", (long long)new_position);
    printf("Read data: %s\n", buffer);


    new_position = lseek(fd, -2, SEEK_CUR);

    if (new_position < 0)
    {
    perror("lseek SEEK_CUR");
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

    printf("SEEK_CUR position: %lld\n", (long long)new_position);
    printf("Read data: %s\n", buffer);

    new_position = lseek(fd, -3, SEEK_END);

    if (new_position < 0)
    {
        perror("lseek SEEK_END");
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

    printf("SEEK_END position: %lld\n",
       (long long)new_position);

    printf("Read data: %s\n", buffer); 

    close(fd);
    
    return 0;
}