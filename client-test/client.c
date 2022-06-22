#include <stdio.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>

#include "myioctl.h"
int main() {
  int fd;
  fd = open("/mnt/ramfs", O_RDONLY | __O_DIRECTORY);
  if (fd< 0) {
    exit(1);
  }
  int a = 0;

  int ret;
  ret = ioctl(fd, IOCTL_NUM1, &a); 
  return ret;
}
