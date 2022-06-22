#include <linux/ioctl.h>
#define IOC_CODE    '1'
#define IOC_NO(x)   (IOC_CODE + (x))
#define MY_IOC(n)   _IO(IOC_CODE, IOC_NO(n))
#define IOCTL_NUM1  MY_IOC(1)
#define IOCTL_NUM2  MY_IOC(2)
