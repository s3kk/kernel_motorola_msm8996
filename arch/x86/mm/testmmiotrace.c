/*
 * Written by Pekka Paalanen, 2008-2009 <pq@iki.fi>
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/mmiotrace.h>

#define MODULE_NAME "testmmiotrace"

static unsigned long mmio_address;
module_param(mmio_address, ulong, 0);
MODULE_PARM_DESC(mmio_address, " Start address of the mapping of 16 kB "
				"(or 8 MB if read_far is non-zero).");

static unsigned long read_far = 0x400100;
module_param(read_far, ulong, 0);
MODULE_PARM_DESC(read_far, " Offset of a 32-bit read within 8 MB "
				"(default: 0x400100).");

static unsigned v16(unsigned i)
{
	return i * 12 + 7;
}

static unsigned v32(unsigned i)
{
	return i * 212371 + 13;
}

static void do_write_test(void __iomem *p)
{
	unsigned int i;
	pr_info(MODULE_NAME ": write test.\n");
	mmiotrace_printk("Write test.\n");

	for (i = 0; i < 256; i++)
		iowrite8(i, p + i);

	for (i = 1024; i < (5 * 1024); i += 2)
		iowrite16(v16(i), p + i);

	for (i = (5 * 1024); i < (16 * 1024); i += 4)
		iowrite32(v32(i), p + i);
}

static void do_read_test(void __iomem *p)
{
	unsigned int i;
	unsigned errs[3] = { 0 };
	pr_info(MODULE_NAME ": read test.\n");
	mmiotrace_printk("Read test.\n");

	for (i = 0; i < 256; i++)
		if (ioread8(p + i) != i)
			++errs[0];

	for (i = 1024; i < (5 * 1024); i += 2)
		if (ioread16(p + i) != v16(i))
			++errs[1];

	for (i = (5 * 1024); i < (16 * 1024); i += 4)
		if (ioread32(p + i) != v32(i))
			++errs[2];

	mmiotrace_printk("Read errors: 8-bit %d, 16-bit %d, 32-bit %d.\n",
						errs[0], errs[1], errs[2]);
}

static void do_read_far_test(void __iomem *p)
{
	pr_info(MODULE_NAME ": read far test.\n");
	mmiotrace_printk("Read far test.\n");

	ioread32(p + read_far);
}

static void do_test(unsigned long size)
{
	void __iomem *p = ioremap_nocache(mmio_address, size);
	if (!p) {
		pr_err(MODULE_NAME ": could not ioremap, aborting.\n");
		return;
	}
	mmiotrace_printk("ioremap returned %p.\n", p);
	do_write_test(p);
	do_read_test(p);
	if (read_far && read_far < size - 4)
		do_read_far_test(p);
	iounmap(p);
}

static int __init init(void)
{
	unsigned long size = (read_far) ? (8 << 20) : (16 << 10);

	if (mmio_address == 0) {
		pr_err(MODULE_NAME ": you have to use the module argument "
							"mmio_address.\n");
		pr_err(MODULE_NAME ": DO NOT LOAD THIS MODULE UNLESS"
				" YOU REALLY KNOW WHAT YOU ARE DOING!\n");
		return -ENXIO;
	}

	pr_warning(MODULE_NAME ": WARNING: mapping %lu kB @ 0x%08lx in PCI "
		"address space, and writing 16 kB of rubbish in there.\n",
		 size >> 10, mmio_address);
	do_test(size);
	pr_info(MODULE_NAME ": All done.\n");
	return 0;
}

static void __exit cleanup(void)
{
	pr_debug(MODULE_NAME ": unloaded.\n");
}

module_init(init);
module_exit(cleanup);
MODULE_LICENSE("GPL");
