#ifndef __ASM_SH_MOBILE_CEU_H__
#define __ASM_SH_MOBILE_CEU_H__

#include <media/soc_camera.h>

struct sh_mobile_ceu_info {
	unsigned long flags; /* SOCAM_... */
	void (*enable_camera)(void);
	void (*disable_camera)(void);
};

#endif /* __ASM_SH_MOBILE_CEU_H__ */
