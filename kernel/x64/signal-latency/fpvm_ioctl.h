#include <linux/ioctl.h>

#define MAJOR_NUM 17
#define MAX_MINORS 1

#define FPVM_IOC_TYPE 0x44
#define FPVM_IOCTL_REG _IOW(FPVM_IOC_TYPE, 1, void *)
#define FPVM_IOCTL_UNREG _IOW(FPVM_IOC_TYPE, 2, void *)