/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <stdarg.h>
#include <stddef.h>
#include "string.h"
#include "stdio.h"
#include "prom.h"

int (*prom)(void *);
phandle chosen_handle;
ihandle stdout;

int call_prom(const char *service, int nargs, int nret, ...)
{
	int i;
	struct prom_args {
		const char *service;
		int nargs;
		int nret;
		unsigned int args[12];
	} args;
	va_list list;

	args.service = service;
	args.nargs = nargs;
	args.nret = nret;

	va_start(list, nret);
	for (i = 0; i < nargs; i++)
		args.args[i] = va_arg(list, unsigned int);
	va_end(list);

	for (i = 0; i < nret; i++)
		args.args[nargs+i] = 0;

	if (prom(&args) < 0)
		return -1;

	return (nret > 0)? args.args[nargs]: 0;
}

int call_prom_ret(const char *service, int nargs, int nret,
		  unsigned int *rets, ...)
{
	int i;
	struct prom_args {
		const char *service;
		int nargs;
		int nret;
		unsigned int args[12];
	} args;
	va_list list;

	args.service = service;
	args.nargs = nargs;
	args.nret = nret;

	va_start(list, rets);
	for (i = 0; i < nargs; i++)
		args.args[i] = va_arg(list, unsigned int);
	va_end(list);

	for (i = 0; i < nret; i++)
		args.args[nargs+i] = 0;

	if (prom(&args) < 0)
		return -1;

	if (rets != (void *) 0)
		for (i = 1; i < nret; ++i)
			rets[i-1] = args.args[nargs+i];

	return (nret > 0)? args.args[nargs]: 0;
}

int write(void *handle, void *ptr, int nb)
{
	return call_prom("write", 3, 1, handle, ptr, nb);
}

/*
 * Older OF's require that when claiming a specific range of addresses,
 * we claim the physical space in the /memory node and the virtual
 * space in the chosen mmu node, and then do a map operation to
 * map virtual to physical.
 */
static int need_map = -1;
static ihandle chosen_mmu;
static phandle memory;

/* returns true if s2 is a prefix of s1 */
static int string_match(const char *s1, const char *s2)
{
	for (; *s2; ++s2)
		if (*s1++ != *s2)
			return 0;
	return 1;
}

static int check_of_version(void)
{
	phandle oprom, chosen;
	char version[64];

	oprom = finddevice("/openprom");
	if (oprom == (phandle) -1)
		return 0;
	if (getprop(oprom, "model", version, sizeof(version)) <= 0)
		return 0;
	version[sizeof(version)-1] = 0;
	printf("OF version = '%s'\r\n", version);
	if (!string_match(version, "Open Firmware, 1.")
	    && !string_match(version, "FirmWorks,3."))
		return 0;
	chosen = finddevice("/chosen");
	if (chosen == (phandle) -1) {
		chosen = finddevice("/chosen@0");
		if (chosen == (phandle) -1) {
			printf("no chosen\n");
			return 0;
		}
	}
	if (getprop(chosen, "mmu", &chosen_mmu, sizeof(chosen_mmu)) <= 0) {
		printf("no mmu\n");
		return 0;
	}
	memory = (ihandle) call_prom("open", 1, 1, "/memory");
	if (memory == (ihandle) -1) {
		memory = (ihandle) call_prom("open", 1, 1, "/memory@0");
		if (memory == (ihandle) -1) {
			printf("no memory node\n");
			return 0;
		}
	}
	printf("old OF detected\r\n");
	return 1;
}

void *claim(unsigned long virt, unsigned long size, unsigned long align)
{
	int ret;
	unsigned int result;

	if (need_map < 0)
		need_map = check_of_version();
	if (align || !need_map)
		return (void *) call_prom("claim", 3, 1, virt, size, align);
	
	ret = call_prom_ret("call-method", 5, 2, &result, "claim", memory,
			    align, size, virt);
	if (ret != 0 || result == -1)
		return (void *) -1;
	ret = call_prom_ret("call-method", 5, 2, &result, "claim", chosen_mmu,
			    align, size, virt);
	/* 0x12 == coherent + read/write */
	ret = call_prom("call-method", 6, 1, "map", chosen_mmu,
			0x12, size, virt, virt);
	return (void *) virt;
}
