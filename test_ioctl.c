#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define AHMET_IOCTL_MAGIC 'A'

#define AHMET_CLEAR_BUFFER \
    _IO(AHMET_IOCTL_MAGIC, 1)


#define AHMET_GET_BUFFER_SIZE \
    _IOR(AHMET_IOCTL_MAGIC, 2, size_t)

int main(void)
{
    const char *message = "Hello Kernel!";
    int fd;
    size_t size;

    fd = open("/dev/ahmet", O_RDWR);

    
    if(fd < 0)
    {
        perror("open");
        return -1;
    }

    ssize_t written;

    written = write(fd, message, strlen(message)); 
    if (written < 0)
    {
        perror("write");
        close(fd);
        return -1;
    }

    printf("Written %zd bytes\n", written);

    if (ioctl(fd, AHMET_GET_BUFFER_SIZE, &size) < 0)
    {
        perror("ioctl");
        close(fd);
        return -1;
    }

    
    printf("Buffer size before clear: %zu\n", size);

    if (ioctl(fd, AHMET_CLEAR_BUFFER) < 0)
    {
        perror("ioctl");
        close(fd);
        return -1;
    }

    if (ioctl(fd, AHMET_GET_BUFFER_SIZE, &size) < 0)
    {
        perror("ioctl");
        close(fd);
        return -1;
    }

    printf("Buffer size after clear: %zu\n", size);

    close(fd);

    return 0;
}