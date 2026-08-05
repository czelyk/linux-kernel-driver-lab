#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>

int main(void)
{
int fd;
int ret;
struct pollfd pfd;
char buffer[128];

fd= open("/dev/ahmet0", O_RDONLY);

if (fd<0)
{
perror("open");
return 1;
}

printf("Device opened\n");

pfd.fd = fd;
pfd.events = POLLIN;
pfd.revents = 0;

printf("Waiting.. \n");

ret = poll(&pfd, 1, -1);

if (ret < 0)
{
perror("poll");
return 1;
}


if (pfd.revents & POLLIN)
{
ssize_t bytes_read;

bytes_read = read(fd, buffer, sizeof(buffer) -1);

if(bytes_read < 0)
{
perror("read");
close(fd);
return 1;
}

buffer[bytes_read] = '\0';

printf("Data received %s\n", buffer);
}

close(fd);
return 0;
}
