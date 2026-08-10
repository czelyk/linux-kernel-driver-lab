#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(void)
{
    int fd;

    printf("PID %d: open() cagriliyor...\n", getpid());
    fflush(stdout);

    fd = open("/dev/ahmet_fifo0", O_RDONLY);

    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    printf("PID %d: cihaz acildi.\n", getpid());
    printf("PID %d: 10 saniye cihaz acik tutuluyor.\n", getpid());
    fflush(stdout);

    sleep(10);

    printf("PID %d: close() yapiliyor.\n", getpid());

    close(fd);

    return 0;
}