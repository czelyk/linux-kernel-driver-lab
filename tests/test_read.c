#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
    int fd;
    char buffer[100];

    fd = open("/dev/ahmet", O_RDWR);

    if(fd < 0)
    {
        perror("open");
        return -1;
    }


    const char *message = "Hello Kernel!";

    if(write(fd, message, strlen(message)) < 0)
    {
        perror("write");
        close(fd);
        return -1;
    }

    memset(buffer, 0, sizeof(buffer));


    int ret = read(fd, buffer, sizeof(buffer));

    if(ret < 0)
    {
        perror("read");
        close(fd);
        return -1;
    }

    printf("Read bytes: %d\n", ret);
    printf("Data from kernel: %s\n", buffer);

    close(fd);

    return 0;
}