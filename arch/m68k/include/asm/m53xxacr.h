/****************************************************************************/

/*
 * m53xxacr.h -- ColdFire version 3 core cache support
 *
 * (C) Copyright 2010, Greg Ungerer <gerg@snapgear.com>
 */

/****************************************************************************/
#ifndef m53xxacr_h
#define m53xxacr_h
/****************************************************************************/

/*
 * All varients of the ColdFire using version 3 cores have a similar
 * cache setup. They have a unified instruction and data cache, with
 * configurable write-through or copy-back operation.
 */

/*
 * Define the Cache Control register flags.
 */
#define CACR_EC		0x80000000	/* Enable cache */
#define CACR_ESB	0x20000000	/* Enable store buffer */
#define CACR_DPI	0x10000000	/* Disable invalidation by CPUSHL */
#define CACR_HLCK	0x08000000	/* Half cache lock mode */
#define CACR_CINVA	0x01000000	/* Invalidate cache */
#define CACR_DNFB	0x00000400	/* Inhibited fill buffer */
#define CACR_DCM_WT	0x00000000	/* Cacheable write-through */
#define CACR_DCM_CB	0x00000100	/* Cacheable copy-back */
#define CACR_DCM_PRE	0x00000200	/* Cache inhibited, precise */
#define CACR_DCM_IMPRE	0x00000300	/* Cache inhibited, imprecise */
#define CACR_WPROTECT	0x00000020	/* Write protect*/
#define CACR_EUSP	0x00000010	/* Eanble separate user a7 */

/*
 * Define the Access Control register flags.
 */
#define ACR_BASE_POS	24		/* Address Base (upper 8 bits) */
#define ACR_MASK_POS	16		/* Address Mask (next 8 bits) */
#define ACR_ENABLE	0x00008000	/* Enable this ACR */
#define ACR_USER	0x00000000	/* Allow only user accesses */
#define ACR_SUPER	0x00002000	/* Allow supervisor access only */
#define ACR_ANY		0x00004000	/* Allow any access type */
#define ACR_CM_WT	0x00000000	/* Cacheable, write-through */
#define ACR_CM_CB	0x00000020	/* Cacheable, copy-back */
#define ACR_CM_PRE	0x00000040	/* Cache inhibited, precise */
#define ACR_CM_IMPRE	0x00000060	/* Cache inhibited, imprecise */
#define ACR_WPROTECT	0x00000004	/* Write protect region */

/*
 * Set the cache controller settings we will use. This default in the
 * CACR is cache inhibited, we use the ACR register to set cacheing
 * enabled on the regions we want (eg RAM).
 */
#ifdef CONFIG_COLDFIRE_SW_A7
#define CACHE_MODE	(CACR_EC + CACR_ESB + CACR_DCM_PRE)
#else
#define CACHE_MODE	(CACR_EC + CACR_ESB + CACR_DCM_PRE + CACR_EUSP)
#endif

#define CACHE_INIT	CACR_CINVA

#define ACR0_MODE	((CONFIG_RAMBASE & 0xff000000) + \
			 (0x000f0000) + \
			 (ACR_ENABLE + ACR_ANY + ACR_CM_CB))
#define ACR1_MODE	0

/****************************************************************************/
#endif  /* m53xxsim_h */
