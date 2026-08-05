#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

int main(void)
{
    int fd;
    long page_size;
    char *mapped_buffer;

    page_size = sysconf(_SC_PAGESIZE);

    if (page_size < 0)
    {
        perror("sysconf");
        return 1;
    }

    fd = open("/dev/ahmet0", O_RDWR);
    
    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    printf("Page size: %ld bytes\n", page_size);

    mapped_buffer = mmap(
                        NULL,
                        page_size,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED,
                        fd,
                        0
                        );

    if (mapped_buffer == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        return 1;
    }
    printf("Memory mapped successfully\n");

    munmap(mapped_buffer, page_size);

    close(fd);

    return 0;
}
