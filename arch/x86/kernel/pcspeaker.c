#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/init.h>

static __init int add_pcspkr(void)
{
	struct platform_device *pd;
	int ret;

	pd = platform_device_alloc("pcspkr", -1);
	if (!pd)
		return -ENOMEM;

	ret = platform_device_add(pd);
	if (ret)
		platform_device_put(pd);

	return ret;

	pd = platform_device_register_simple("pcspkr", -1, NULL, 0);

	return IS_ERR(pd) ? PTR_ERR(pd) : 0;
}
device_initcall(add_pcspkr);
