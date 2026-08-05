#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>


void sigio_handler(int sig)
{
    const char message[] = "SIGIO received!\n";

    (void)sig;

    write(STDOUT_FILENO,
          message,
          sizeof(message) - 1);
}


int main(void)
{
 
int fd;

fd = open("/dev/ahmet0", O_RDWR);

if (fd < 0)
{
    perror("open");
    return 1;
}

signal(SIGIO, sigio_handler);  

fcntl(fd, F_SETOWN, getpid());

int flags;

flags = fcntl(fd, F_GETFL);

if(flags < 0)
{
    perror("fcntl F_GETFL");
    close(fd);
    return 1;
}

if (fcntl(fd, F_SETFL, flags | O_ASYNC) < 0)
{
    perror("fcntl F_SETFL");
    close(fd);
    return 1;
}

printf("Waiting for SIGIO...\n");

while (1)
{
    pause();
}


return 0; 

}
