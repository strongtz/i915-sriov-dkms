#include <linux/pm_runtime.h>

int main(void)
{
  return (int)(pm_runtime_get_if_active((struct device *)0xdeadbeef));
}

// build success: #define _CONFIGURE_PM_RUNTIME_GET_IF_ACTIVE KERNEL_VERSION(6, 9, 0)