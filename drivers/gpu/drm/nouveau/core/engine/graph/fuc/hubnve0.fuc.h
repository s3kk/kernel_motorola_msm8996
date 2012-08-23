uint32_t nve0_grhub_data[] = {
/* 0x0000: gpc_count */
	0x00000000,
/* 0x0004: rop_count */
	0x00000000,
/* 0x0008: cmd_queue */
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
/* 0x0050: hub_mmio_list_head */
	0x00000000,
/* 0x0054: hub_mmio_list_tail */
	0x00000000,
/* 0x0058: ctx_current */
	0x00000000,
/* 0x005c: chipsets */
	0x000000e4,
	0x013c0070,
	0x000000e7,
	0x013c0070,
	0x00000000,
/* 0x0070: nve4_hub_mmio_head */
	0x0417e91c,
	0x04400204,
	0x18404010,
	0x204040a8,
	0x184040d0,
	0x004040f8,
	0x08404130,
	0x08404150,
	0x00404164,
	0x0c4041a0,
	0x0c404200,
	0x34404404,
	0x0c404460,
	0x00404480,
	0x00404498,
	0x0c404604,
	0x0c404618,
	0x0440462c,
	0x00404640,
	0x00404654,
	0x00404660,
	0x48404678,
	0x084046c8,
	0x08404700,
	0x24404718,
	0x04404744,
	0x00404754,
	0x00405800,
	0x08405830,
	0x00405854,
	0x0c405870,
	0x04405a00,
	0x00405a18,
	0x00405b00,
	0x00405b10,
	0x00406020,
	0x0c406028,
	0x044064a8,
	0x044064b4,
	0x2c4064c0,
	0x004064fc,
	0x00407040,
	0x00407804,
	0x1440780c,
	0x004078bc,
	0x18408000,
	0x00408064,
	0x08408800,
	0x00408840,
	0x08408900,
	0x00408980,
/* 0x013c: nve4_hub_mmio_tail */
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
/* 0x0200: chan_data */
/* 0x0200: chan_mmio_count */
	0x00000000,
/* 0x0204: chan_mmio_address */
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
/* 0x0300: xfer_data */
	0x00000000,
};

uint32_t nve0_grhub_code[] = {
	0x03090ef5,
/* 0x0004: queue_put */
	0x9800d898,
	0x86f001d9,
	0x0489b808,
	0xf00c1bf4,
	0x21f502f7,
	0x00f802ec,
/* 0x001c: queue_put_next */
	0xb60798c4,
	0x8dbb0384,
	0x0880b600,
	0x80008e80,
	0x90b6018f,
	0x0f94f001,
	0xf801d980,
/* 0x0039: queue_get */
	0x0131f400,
	0x9800d898,
	0x89b801d9,
	0x210bf404,
	0xb60789c4,
	0x9dbb0394,
	0x0890b600,
	0x98009e98,
	0x80b6019f,
	0x0f84f001,
	0xf400d880,
/* 0x0066: queue_get_done */
	0x00f80132,
/* 0x0068: nv_rd32 */
	0x0728b7f1,
	0xb906b4b6,
	0xc9f002ec,
	0x00bcd01f,
/* 0x0078: nv_rd32_wait */
	0xc800bccf,
	0x1bf41fcc,
	0x06a7f0fa,
	0x010321f5,
	0xf840bfcf,
/* 0x008d: nv_wr32 */
	0x28b7f100,
	0x06b4b607,
	0xb980bfd0,
	0xc9f002ec,
	0x1ec9f01f,
/* 0x00a3: nv_wr32_wait */
	0xcf00bcd0,
	0xccc800bc,
	0xfa1bf41f,
/* 0x00ae: watchdog_reset */
	0x87f100f8,
	0x84b60430,
	0x1ff9f006,
	0xf8008fd0,
/* 0x00bd: watchdog_clear */
	0x3087f100,
	0x0684b604,
	0xf80080d0,
/* 0x00c9: wait_donez */
	0x3c87f100,
	0x0684b608,
	0x99f094bd,
	0x0089d000,
	0x081887f1,
	0xd00684b6,
/* 0x00e2: wait_done_wait_donez */
	0x87f1008a,
	0x84b60400,
	0x0088cf06,
	0xf4888aff,
	0x87f1f31b,
	0x84b6085c,
	0xf094bd06,
	0x89d00099,
/* 0x0103: wait_doneo */
	0xf100f800,
	0xb6083c87,
	0x94bd0684,
	0xd00099f0,
	0x87f10089,
	0x84b60818,
	0x008ad006,
/* 0x011c: wait_done_wait_doneo */
	0x040087f1,
	0xcf0684b6,
	0x8aff0088,
	0xf30bf488,
	0x085c87f1,
	0xbd0684b6,
	0x0099f094,
	0xf80089d0,
/* 0x013d: mmctx_size */
/* 0x013f: nv_mmctx_size_loop */
	0x9894bd00,
	0x85b600e8,
	0x0180b61a,
	0xbb0284b6,
	0xe0b60098,
	0x04efb804,
	0xb9eb1bf4,
	0x00f8029f,
/* 0x015c: mmctx_xfer */
	0x083c87f1,
	0xbd0684b6,
	0x0199f094,
	0xf10089d0,
	0xb6071087,
	0x94bd0684,
	0xf405bbfd,
	0x8bd0090b,
	0x0099f000,
/* 0x0180: mmctx_base_disabled */
	0xf405eefd,
	0x8ed00c0b,
	0xc08fd080,
/* 0x018f: mmctx_multi_disabled */
	0xb70199f0,
	0xc8010080,
	0xb4b600ab,
	0x0cb9f010,
	0xb601aec8,
	0xbefd11e4,
	0x008bd005,
/* 0x01a8: mmctx_exec_loop */
/* 0x01a8: mmctx_wait_free */
	0xf0008ecf,
	0x0bf41fe4,
	0x00ce98fa,
	0xd005e9fd,
	0xc0b6c08e,
	0x04cdb804,
	0xc8e81bf4,
	0x1bf402ab,
/* 0x01c9: mmctx_fini_wait */
	0x008bcf18,
	0xb01fb4f0,
	0x1bf410b4,
	0x02a7f0f7,
	0xf4c921f4,
/* 0x01de: mmctx_stop */
	0xabc81b0e,
	0x10b4b600,
	0xf00cb9f0,
	0x8bd012b9,
/* 0x01ed: mmctx_stop_wait */
	0x008bcf00,
	0xf412bbc8,
/* 0x01f6: mmctx_done */
	0x87f1fa1b,
	0x84b6085c,
	0xf094bd06,
	0x89d00199,
/* 0x0207: strand_wait */
	0xf900f800,
	0x02a7f0a0,
	0xfcc921f4,
/* 0x0213: strand_pre */
	0xf100f8a0,
	0xf04afc87,
	0x97f00283,
	0x0089d00c,
	0x020721f5,
/* 0x0226: strand_post */
	0x87f100f8,
	0x83f04afc,
	0x0d97f002,
	0xf50089d0,
	0xf8020721,
/* 0x0239: strand_set */
	0xfca7f100,
	0x02a3f04f,
	0x0500aba2,
	0xd00fc7f0,
	0xc7f000ac,
	0x00bcd00b,
	0x020721f5,
	0xf000aed0,
	0xbcd00ac7,
	0x0721f500,
/* 0x0263: strand_ctx_init */
	0xf100f802,
	0xb6083c87,
	0x94bd0684,
	0xd00399f0,
	0x21f50089,
	0xe7f00213,
	0x3921f503,
	0xfca7f102,
	0x02a3f046,
	0x0400aba0,
	0xf040a0d0,
	0xbcd001c7,
	0x0721f500,
	0x010c9202,
	0xf000acd0,
	0xbcd002c7,
	0x0721f500,
	0x2621f502,
	0x8087f102,
	0x0684b608,
	0xb70089cf,
	0x95220080,
/* 0x02ba: ctx_init_strand_loop */
	0x8ed008fe,
	0x408ed000,
	0xb6808acf,
	0xa0b606a5,
	0x00eabb01,
	0xb60480b6,
	0x1bf40192,
	0x08e4b6e8,
	0xf1f2efbc,
	0xb6085c87,
	0x94bd0684,
	0xd00399f0,
	0x00f80089,
/* 0x02ec: error */
	0xe7f1e0f9,
	0xe4b60814,
	0x00efd006,
	0x0c1ce7f1,
	0xf006e4b6,
	0xefd001f7,
	0xf8e0fc00,
/* 0x0309: init */
	0xfe04bd00,
	0x07fe0004,
	0x0017f100,
	0x0227f012,
	0xf10012d0,
	0xfe05b917,
	0x17f10010,
	0x10d00400,
	0x0437f1c0,
	0x0634b604,
	0x200327f1,
	0xf10032d0,
	0xd0200427,
	0x27f10132,
	0x32d0200b,
	0x0c27f102,
	0x0732d020,
	0x0c2427f1,
	0xb90624b6,
	0x23d00003,
	0x0427f100,
	0x0023f087,
	0xb70012d0,
	0xf0010012,
	0x12d00427,
	0x1031f400,
	0x9604e7f1,
	0xf440e3f0,
	0xf1c76821,
	0x01018090,
	0x801ff4f0,
	0x17f0000f,
	0x041fbb01,
	0xf10112b6,
	0xb6040c27,
	0x21d00624,
	0x4021d000,
	0x080027f1,
	0xcf0624b6,
	0xf7f00022,
/* 0x03a9: init_find_chipset */
	0x08f0b654,
	0xb800f398,
	0x0bf40432,
	0x0034b00b,
	0xf8f11bf4,
/* 0x03bd: init_context */
	0x0017f100,
	0x02fe5801,
	0xf003ff58,
	0x0e8000e3,
	0x150f8014,
	0x013d21f5,
	0x070037f1,
	0x950634b6,
	0x34d00814,
	0x4034d000,
	0x130030b7,
	0xb6001fbb,
	0x3fd002f5,
	0x0815b600,
	0xb60110b6,
	0x1fb90814,
	0x6321f502,
	0x001fbb02,
	0xf1000398,
	0xf0200047,
/* 0x040e: init_gpc */
	0x4ea05043,
	0x1fb90804,
	0x8d21f402,
	0x08004ea0,
	0xf4022fb9,
	0x4ea08d21,
	0xf4bd010c,
	0xa08d21f4,
	0xf401044e,
	0x4ea08d21,
	0xf7f00100,
	0x8d21f402,
	0x08004ea0,
/* 0x0440: init_gpc_wait */
	0xc86821f4,
	0x0bf41fff,
	0x044ea0fa,
	0x6821f408,
	0xb7001fbb,
	0xb6800040,
	0x1bf40132,
	0x0027f1b4,
	0x0624b608,
	0xb74021d0,
	0xbd080020,
	0x1f19f014,
/* 0x0473: main */
	0xf40021d0,
	0x28f40031,
	0x08d7f000,
	0xf43921f4,
	0xe4b1f401,
	0x1bf54001,
	0x87f100d1,
	0x84b6083c,
	0xf094bd06,
	0x89d00499,
	0x0017f100,
	0x0614b60b,
	0xcf4012cf,
	0x13c80011,
	0x7e0bf41f,
	0xf41f23c8,
	0x20f95a0b,
	0xf10212b9,
	0xb6083c87,
	0x94bd0684,
	0xd00799f0,
	0x32f40089,
	0x0231f401,
	0x082921f5,
	0x085c87f1,
	0xbd0684b6,
	0x0799f094,
	0xfc0089d0,
	0x3c87f120,
	0x0684b608,
	0x99f094bd,
	0x0089d006,
	0xf50131f4,
	0xf1082921,
	0xb6085c87,
	0x94bd0684,
	0xd00699f0,
	0x0ef40089,
/* 0x0509: chsw_prev_no_next */
	0xb920f931,
	0x32f40212,
	0x0232f401,
	0x082921f5,
	0x17f120fc,
	0x14b60b00,
	0x0012d006,
/* 0x0527: chsw_no_prev */
	0xc8130ef4,
	0x0bf41f23,
	0x0131f40d,
	0xf50232f4,
/* 0x0537: chsw_done */
	0xf1082921,
	0xb60b0c17,
	0x27f00614,
	0x0012d001,
	0x085c87f1,
	0xbd0684b6,
	0x0499f094,
	0xf50089d0,
/* 0x0557: main_not_ctx_switch */
	0xb0ff200e,
	0x1bf401e4,
	0x02f2b90d,
	0x07b521f5,
/* 0x0567: main_not_ctx_chan */
	0xb0420ef4,
	0x1bf402e4,
	0x3c87f12e,
	0x0684b608,
	0x99f094bd,
	0x0089d007,
	0xf40132f4,
	0x21f50232,
	0x87f10829,
	0x84b6085c,
	0xf094bd06,
	0x89d00799,
	0x110ef400,
/* 0x0598: main_not_ctx_save */
	0xf010ef94,
	0x21f501f5,
	0x0ef502ec,
/* 0x05a6: main_done */
	0x17f1fed1,
	0x14b60820,
	0xf024bd06,
	0x12d01f29,
	0xbe0ef500,
/* 0x05b9: ih */
	0xfe80f9fe,
	0x80f90188,
	0xa0f990f9,
	0xd0f9b0f9,
	0xf0f9e0f9,
	0xc4800acf,
	0x0bf404ab,
	0x00b7f11d,
	0x08d7f019,
	0xcf40becf,
	0x21f400bf,
	0x00b0b704,
	0x01e7f004,
/* 0x05ef: ih_no_fifo */
	0xe400bed0,
	0xf40100ab,
	0xd7f00d0b,
	0x01e7f108,
	0x0421f440,
/* 0x0600: ih_no_ctxsw */
	0x0104b7f1,
	0xabffb0bd,
	0x0d0bf4b4,
	0x0c1ca7f1,
	0xd006a4b6,
/* 0x0616: ih_no_other */
	0x0ad000ab,
	0xfcf0fc40,
	0xfcd0fce0,
	0xfca0fcb0,
	0xfe80fc90,
	0x80fc0088,
	0xf80032f4,
/* 0x0631: ctx_4160s */
	0x60e7f101,
	0x40e3f041,
	0xf401f7f0,
/* 0x063e: ctx_4160s_wait */
	0x21f48d21,
	0x04ffc868,
	0xf8fa0bf4,
/* 0x0649: ctx_4160c */
	0x60e7f100,
	0x40e3f041,
	0x21f4f4bd,
/* 0x0657: ctx_4170s */
	0xf100f88d,
	0xf04170e7,
	0xf5f040e3,
	0x8d21f410,
/* 0x0666: ctx_4170w */
	0xe7f100f8,
	0xe3f04170,
	0x6821f440,
	0xf410f4f0,
	0x00f8f31b,
/* 0x0678: ctx_redswitch */
	0x0614e7f1,
	0xf106e4b6,
	0xd00270f7,
	0xf7f000ef,
/* 0x0689: ctx_redswitch_delay */
	0x01f2b608,
	0xf1fd1bf4,
	0xd00770f7,
	0x00f800ef,
/* 0x0698: ctx_86c */
	0x086ce7f1,
	0xd006e4b6,
	0xe7f100ef,
	0xe3f08a14,
	0x8d21f440,
	0xa86ce7f1,
	0xf441e3f0,
	0x00f88d21,
/* 0x06b8: ctx_load */
	0x083c87f1,
	0xbd0684b6,
	0x0599f094,
	0xf00089d0,
	0x21f40ca7,
	0x2417f1c9,
	0x0614b60a,
	0xf10010d0,
	0xb60b0037,
	0x32d00634,
	0x0c17f140,
	0x0614b60a,
	0xd00747f0,
	0x14d00012,
/* 0x06f1: ctx_chan_wait_0 */
	0x4014cf40,
	0xf41f44f0,
	0x32d0fa1b,
	0x000bfe00,
	0xb61f2af0,
	0x20b60424,
	0x3c87f102,
	0x0684b608,
	0x99f094bd,
	0x0089d008,
	0x0a0417f1,
	0xd00614b6,
	0x17f10012,
	0x14b60a20,
	0x0227f006,
	0x800023f1,
	0xf00012d0,
	0x27f11017,
	0x23f00300,
	0x0512fa02,
	0x87f103f8,
	0x84b6085c,
	0xf094bd06,
	0x89d00899,
	0xc1019800,
	0x981814b6,
	0x25b6c002,
	0x0512fd08,
	0xf1160180,
	0xb6083c87,
	0x94bd0684,
	0xd00999f0,
	0x27f10089,
	0x24b60a04,
	0x0021d006,
	0xf10127f0,
	0xb60a2017,
	0x12d00614,
	0x0017f100,
	0x0613f002,
	0xf80501fa,
	0x5c87f103,
	0x0684b608,
	0x99f094bd,
	0x0089d009,
	0x085c87f1,
	0xbd0684b6,
	0x0599f094,
	0xf80089d0,
/* 0x07b5: ctx_chan */
	0x3121f500,
	0xb821f506,
	0x0ca7f006,
	0xf1c921f4,
	0xb60a1017,
	0x27f00614,
	0x0012d005,
/* 0x07d0: ctx_chan_wait */
	0xfd0012cf,
	0x1bf40522,
	0x4921f5fa,
/* 0x07df: ctx_mmio_exec */
	0x9800f806,
	0x27f18103,
	0x24b60a04,
	0x0023d006,
/* 0x07ee: ctx_mmio_loop */
	0x34c434bd,
	0x0f1bf4ff,
	0x030057f1,
	0xfa0653f0,
	0x03f80535,
/* 0x0800: ctx_mmio_pull */
	0x98c04e98,
	0x21f4c14f,
	0x0830b68d,
	0xf40112b6,
/* 0x0812: ctx_mmio_done */
	0x0398df1b,
	0x0023d016,
	0xf1800080,
	0xf0020017,
	0x01fa0613,
	0xf803f806,
/* 0x0829: ctx_xfer */
	0x0611f400,
/* 0x082f: ctx_xfer_pre */
	0xf01102f4,
	0x21f510f7,
	0x21f50698,
	0x11f40631,
/* 0x083d: ctx_xfer_pre_load */
	0x02f7f01c,
	0x065721f5,
	0x066621f5,
	0x067821f5,
	0x21f5f4bd,
	0x21f50657,
/* 0x0856: ctx_xfer_exec */
	0x019806b8,
	0x1427f116,
	0x0624b604,
	0xf10020d0,
	0xf0a500e7,
	0x1fb941e3,
	0x8d21f402,
	0xf004e0b6,
	0x2cf001fc,
	0x0124b602,
	0xf405f2fd,
	0x17f18d21,
	0x13f04afc,
	0x0c27f002,
	0xf50012d0,
	0xf1020721,
	0xf047fc27,
	0x20d00223,
	0x012cf000,
	0xd00320b6,
	0xacf00012,
	0x06a5f001,
	0x9800b7f0,
	0x0d98140c,
	0x00e7f015,
	0x015c21f5,
	0xf508a7f0,
	0xf5010321,
	0xf4020721,
	0xa7f02201,
	0xc921f40c,
	0x0a1017f1,
	0xf00614b6,
	0x12d00527,
/* 0x08dd: ctx_xfer_post_save_wait */
	0x0012cf00,
	0xf40522fd,
	0x02f4fa1b,
/* 0x08e9: ctx_xfer_post */
	0x02f7f032,
	0x065721f5,
	0x21f5f4bd,
	0x21f50698,
	0x21f50226,
	0xf4bd0666,
	0x065721f5,
	0x981011f4,
	0x11fd8001,
	0x070bf405,
	0x07df21f5,
/* 0x0914: ctx_xfer_no_post_mmio */
	0x064921f5,
/* 0x0918: ctx_xfer_done */
	0x000000f8,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
};
