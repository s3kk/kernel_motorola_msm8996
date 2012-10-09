/*
 * Copyright (C) 2010 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <subdev/fb.h>

struct nv30_fb_priv {
	struct nouveau_fb base;
};

void
nv30_fb_tile_init(struct nouveau_fb *pfb, int i, u32 addr, u32 size, u32 pitch,
		  u32 flags, struct nouveau_fb_tile *tile)
{
	tile->addr = addr | 1;
	tile->limit = max(1u, addr + size) - 1;
	tile->pitch = pitch;
}

void
nv30_fb_tile_fini(struct nouveau_fb *pfb, int i, struct nouveau_fb_tile *tile)
{
	tile->addr  = 0;
	tile->limit = 0;
	tile->pitch = 0;
}

static int
calc_bias(struct nv30_fb_priv *priv, int k, int i, int j)
{
	struct nouveau_device *device = nv_device(priv);
	int b = (device->chipset > 0x30 ?
		 nv_rd32(priv, 0x122c + 0x10 * k + 0x4 * j) >> (4 * (i ^ 1)) :
		 0) & 0xf;

	return 2 * (b & 0x8 ? b - 0x10 : b);
}

static int
calc_ref(struct nv30_fb_priv *priv, int l, int k, int i)
{
	int j, x = 0;

	for (j = 0; j < 4; j++) {
		int m = (l >> (8 * i) & 0xff) + calc_bias(priv, k, i, j);

		x |= (0x80 | clamp(m, 0, 0x1f)) << (8 * j);
	}

	return x;
}

int
nv30_fb_init(struct nouveau_object *object)
{
	struct nouveau_device *device = nv_device(object);
	struct nv30_fb_priv *priv = (void *)object;
	int ret, i, j;

	ret = nouveau_fb_init(&priv->base);
	if (ret)
		return ret;

	/* Init the memory timing regs at 0x10037c/0x1003ac */
	if (device->chipset == 0x30 ||
	    device->chipset == 0x31 ||
	    device->chipset == 0x35) {
		/* Related to ROP count */
		int n = (device->chipset == 0x31 ? 2 : 4);
		int l = nv_rd32(priv, 0x1003d0);

		for (i = 0; i < n; i++) {
			for (j = 0; j < 3; j++)
				nv_wr32(priv, 0x10037c + 0xc * i + 0x4 * j,
					calc_ref(priv, l, 0, j));

			for (j = 0; j < 2; j++)
				nv_wr32(priv, 0x1003ac + 0x8 * i + 0x4 * j,
					calc_ref(priv, l, 1, j));
		}
	}

	return 0;
}

static int
nv30_fb_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	     struct nouveau_oclass *oclass, void *data, u32 size,
	     struct nouveau_object **pobject)
{
	struct nv30_fb_priv *priv;
	u32 pbus1218;
	int ret;

	ret = nouveau_fb_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	pbus1218 = nv_rd32(priv, 0x001218);
	switch (pbus1218 & 0x00000300) {
	case 0x00000000: priv->base.ram.type = NV_MEM_TYPE_SDRAM; break;
	case 0x00000100: priv->base.ram.type = NV_MEM_TYPE_DDR1; break;
	case 0x00000200: priv->base.ram.type = NV_MEM_TYPE_GDDR3; break;
	case 0x00000300: priv->base.ram.type = NV_MEM_TYPE_GDDR2; break;
	}
	priv->base.ram.size = nv_rd32(priv, 0x10020c) & 0xff000000;

	priv->base.memtype_valid = nv04_fb_memtype_valid;
	priv->base.tile.regions = 8;
	priv->base.tile.init = nv30_fb_tile_init;
	priv->base.tile.fini = nv30_fb_tile_fini;
	priv->base.tile.prog = nv10_fb_tile_prog;
	return nouveau_fb_created(&priv->base);
}

struct nouveau_oclass
nv30_fb_oclass = {
	.handle = NV_SUBDEV(FB, 0x30),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv30_fb_ctor,
		.dtor = _nouveau_fb_dtor,
		.init = nv30_fb_init,
		.fini = _nouveau_fb_fini,
	},
};
