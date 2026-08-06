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

#define AHMET_GET_BUFFER_CAPACITY \
        _IOR(AHMET_IOCTL_MAGIC, 3, size_t)


#define AHMET_FILL_BUFFER \
        _IOW(AHMET_IOCTL_MAGIC, 4, char)

#define AHMET_GET_DEVICE_ID \
        _IOR(AHMET_IOCTL_MAGIC, 5, int)


int main(void)
{
    int device_id;
    char fill_char = '*';
    char read_buffer[32];
    ssize_t bytes_read;
    const char *message = "Hello Kernel!";
    int fd;
    size_t size;
    size_t buffer_capacity;

    fd = open("/dev/ahmet2", O_RDWR);

    
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

    if (ioctl(fd,
          AHMET_GET_BUFFER_CAPACITY,
          &buffer_capacity) < 0)
    {
    perror("ioctl GET_BUFFER_CAPACITY");
    close(fd);
    return 1;
    }

    printf("Buffer capacity: %zu bytes\n",
       buffer_capacity);

    printf("Buffer size after clear: %zu\n", size);

    if (ioctl(fd,
          AHMET_FILL_BUFFER,
          &fill_char) < 0)
    {
    perror("ioctl FILL_BUFFER");
    close(fd);
    return 1;
    }

    if (lseek(fd, 0, SEEK_SET) < 0)
    {
    perror("lseek");
    close(fd);
    return 1;
    }

    bytes_read = read(fd,
                  read_buffer,
                  sizeof(read_buffer) - 1);

    if (bytes_read < 0)
    {
        perror("read");
        close(fd);
        return 1;
    }

    read_buffer[bytes_read] = '\0';

    printf("Filled buffer sample: %s\n",
       read_buffer);


    if (ioctl(fd,
          AHMET_GET_DEVICE_ID,
          &device_id) < 0)
    {
        perror("ioctl GET_DEVICE_ID");
        close(fd);
        return 1;
    }

    printf("Device ID: %d\n", device_id);

    close(fd);

    return 0;
}