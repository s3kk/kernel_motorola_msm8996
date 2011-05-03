/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <bcmdefs.h>
#include <stdarg.h>
#include <bcmutils.h>
#include <bcmnvram.h>
#include <bcmdevs.h>
#include <proto/802.11.h>

struct sk_buff *BCMFASTPATH pkt_buf_get_skb(uint len)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(len);
	if (skb) {
		skb_put(skb, len);
		skb->priority = 0;
	}

	return skb;
}

/* Free the driver packet. Free the tag if present */
void BCMFASTPATH pkt_buf_free_skb(struct sk_buff *skb)
{
	struct sk_buff *nskb;
	int nest = 0;

	/* perversion: we use skb->next to chain multi-skb packets */
	while (skb) {
		nskb = skb->next;
		skb->next = NULL;

		if (skb->destructor)
			/* cannot kfree_skb() on hard IRQ (net/core/skbuff.c) if
			 * destructor exists
			 */
			dev_kfree_skb_any(skb);
		else
			/* can free immediately (even in_irq()) if destructor
			 * does not exist
			 */
			dev_kfree_skb(skb);

		nest++;
		skb = nskb;
	}
}

/* copy a buffer into a pkt buffer chain */
uint pktfrombuf(struct sk_buff *p, uint offset, int len,
		unsigned char *buf)
{
	uint n, ret = 0;

	/* skip 'offset' bytes */
	for (; p && offset; p = p->next) {
		if (offset < (uint) (p->len))
			break;
		offset -= p->len;
	}

	if (!p)
		return 0;

	/* copy the data */
	for (; p && len; p = p->next) {
		n = min((uint) (p->len) - offset, (uint) len);
		memcpy(p->data + offset, buf, n);
		buf += n;
		len -= n;
		ret += n;
		offset = 0;
	}

	return ret;
}
/* return total length of buffer chain */
uint BCMFASTPATH pkttotlen(struct sk_buff *p)
{
	uint total;

	total = 0;
	for (; p; p = p->next)
		total += p->len;
	return total;
}

/*
 * osl multiple-precedence packet queue
 * hi_prec is always >= the number of the highest non-empty precedence
 */
struct sk_buff *BCMFASTPATH pktq_penq(struct pktq *pq, int prec,
				      struct sk_buff *p)
{
	struct pktq_prec *q;

	if (pktq_full(pq) || pktq_pfull(pq, prec))
		return NULL;

	q = &pq->q[prec];

	if (q->head)
		q->tail->prev = p;
	else
		q->head = p;

	q->tail = p;
	q->len++;

	pq->len++;

	if (pq->hi_prec < prec)
		pq->hi_prec = (u8) prec;

	return p;
}

struct sk_buff *BCMFASTPATH pktq_penq_head(struct pktq *pq, int prec,
					   struct sk_buff *p)
{
	struct pktq_prec *q;

	if (pktq_full(pq) || pktq_pfull(pq, prec))
		return NULL;

	q = &pq->q[prec];

	if (q->head == NULL)
		q->tail = p;

	p->prev = q->head;
	q->head = p;
	q->len++;

	pq->len++;

	if (pq->hi_prec < prec)
		pq->hi_prec = (u8) prec;

	return p;
}

struct sk_buff *BCMFASTPATH pktq_pdeq(struct pktq *pq, int prec)
{
	struct pktq_prec *q;
	struct sk_buff *p;

	q = &pq->q[prec];

	p = q->head;
	if (p == NULL)
		return NULL;

	q->head = p->prev;
	if (q->head == NULL)
		q->tail = NULL;

	q->len--;

	pq->len--;

	p->prev = NULL;

	return p;
}

struct sk_buff *BCMFASTPATH pktq_pdeq_tail(struct pktq *pq, int prec)
{
	struct pktq_prec *q;
	struct sk_buff *p, *prev;

	q = &pq->q[prec];

	p = q->head;
	if (p == NULL)
		return NULL;

	for (prev = NULL; p != q->tail; p = p->prev)
		prev = p;

	if (prev)
		prev->prev = NULL;
	else
		q->head = NULL;

	q->tail = prev;
	q->len--;

	pq->len--;

	return p;
}

#ifdef BRCM_FULLMAC
void pktq_pflush(struct pktq *pq, int prec, bool dir)
{
	struct pktq_prec *q;
	struct sk_buff *p;

	q = &pq->q[prec];
	p = q->head;
	while (p) {
		q->head = p->prev;
		p->prev = NULL;
		pkt_buf_free_skb(p);
		q->len--;
		pq->len--;
		p = q->head;
	}
	q->tail = NULL;
}

void pktq_flush(struct pktq *pq, bool dir)
{
	int prec;
	for (prec = 0; prec < pq->num_prec; prec++)
		pktq_pflush(pq, prec, dir);
}
#else /* !BRCM_FULLMAC */
void
pktq_pflush(struct pktq *pq, int prec, bool dir,
	    ifpkt_cb_t fn, int arg)
{
	struct pktq_prec *q;
	struct sk_buff *p, *prev = NULL;

	q = &pq->q[prec];
	p = q->head;
	while (p) {
		if (fn == NULL || (*fn) (p, arg)) {
			bool head = (p == q->head);
			if (head)
				q->head = p->prev;
			else
				prev->prev = p->prev;
			p->prev = NULL;
			pkt_buf_free_skb(p);
			q->len--;
			pq->len--;
			p = (head ? q->head : prev->prev);
		} else {
			prev = p;
			p = p->prev;
		}
	}

	if (q->head == NULL) {
		q->tail = NULL;
	}
}

void pktq_flush(struct pktq *pq, bool dir,
		ifpkt_cb_t fn, int arg)
{
	int prec;
	for (prec = 0; prec < pq->num_prec; prec++)
		pktq_pflush(pq, prec, dir, fn, arg);
}
#endif /* BRCM_FULLMAC */

void pktq_init(struct pktq *pq, int num_prec, int max_len)
{
	int prec;

	/* pq is variable size; only zero out what's requested */
	memset(pq, 0,
	      offsetof(struct pktq, q) + (sizeof(struct pktq_prec) * num_prec));

	pq->num_prec = (u16) num_prec;

	pq->max = (u16) max_len;

	for (prec = 0; prec < num_prec; prec++)
		pq->q[prec].max = pq->max;
}

struct sk_buff *pktq_peek_tail(struct pktq *pq, int *prec_out)
{
	int prec;

	if (pq->len == 0)
		return NULL;

	for (prec = 0; prec < pq->hi_prec; prec++)
		if (pq->q[prec].head)
			break;

	if (prec_out)
		*prec_out = prec;

	return pq->q[prec].tail;
}

/* Return sum of lengths of a specific set of precedences */
int pktq_mlen(struct pktq *pq, uint prec_bmp)
{
	int prec, len;

	len = 0;

	for (prec = 0; prec <= pq->hi_prec; prec++)
		if (prec_bmp & (1 << prec))
			len += pq->q[prec].len;

	return len;
}
/* Priority dequeue from a specific set of precedences */
struct sk_buff *BCMFASTPATH pktq_mdeq(struct pktq *pq, uint prec_bmp,
				      int *prec_out)
{
	struct pktq_prec *q;
	struct sk_buff *p;
	int prec;

	if (pq->len == 0)
		return NULL;

	while ((prec = pq->hi_prec) > 0 && pq->q[prec].head == NULL)
		pq->hi_prec--;

	while ((prec_bmp & (1 << prec)) == 0 || pq->q[prec].head == NULL)
		if (prec-- == 0)
			return NULL;

	q = &pq->q[prec];

	p = q->head;
	if (p == NULL)
		return NULL;

	q->head = p->prev;
	if (q->head == NULL)
		q->tail = NULL;

	q->len--;

	if (prec_out)
		*prec_out = prec;

	pq->len--;

	p->prev = NULL;

	return p;
}

/* parse a xx:xx:xx:xx:xx:xx format ethernet address */
int bcm_ether_atoe(char *p, u8 *ea)
{
	int i = 0;

	for (;;) {
		ea[i++] = (char)simple_strtoul(p, &p, 16);
		if (!*p++ || i == 6)
			break;
	}

	return i == 6;
}

/*
 * Search the name=value vars for a specific one and return its value.
 * Returns NULL if not found.
 */
char *getvar(char *vars, const char *name)
{
	char *s;
	int len;

	if (!name)
		return NULL;

	len = strlen(name);
	if (len == 0)
		return NULL;

	/* first look in vars[] */
	for (s = vars; s && *s;) {
		if ((memcmp(s, name, len) == 0) && (s[len] == '='))
			return &s[len + 1];

		while (*s++)
			;
	}
#ifdef BRCM_FULLMAC
	return NULL;
#else
	/* then query nvram */
	return nvram_get(name);
#endif
}

/*
 * Search the vars for a specific one and return its value as
 * an integer. Returns 0 if not found.
 */
int getintvar(char *vars, const char *name)
{
	char *val;

	val = getvar(vars, name);
	if (val == NULL)
		return 0;

	return simple_strtoul(val, NULL, 0);
}

#if defined(BCMDBG)
/* pretty hex print a pkt buffer chain */
void prpkt(const char *msg, struct sk_buff *p0)
{
	struct sk_buff *p;

	if (msg && (msg[0] != '\0'))
		printk(KERN_DEBUG "%s:\n", msg);

	for (p = p0; p; p = p->next)
		prhex(NULL, p->data, p->len);
}
#endif				/* defined(BCMDBG) */

/* iovar table lookup */
const bcm_iovar_t *bcm_iovar_lookup(const bcm_iovar_t *table, const char *name)
{
	const bcm_iovar_t *vi;
	const char *lookup_name;

	/* skip any ':' delimited option prefixes */
	lookup_name = strrchr(name, ':');
	if (lookup_name != NULL)
		lookup_name++;
	else
		lookup_name = name;

	for (vi = table; vi->name; vi++) {
		if (!strcmp(vi->name, lookup_name))
			return vi;
	}
	/* ran to end of table */

	return NULL;		/* var name not found */
}

int bcm_iovar_lencheck(const bcm_iovar_t *vi, void *arg, int len, bool set)
{
	int bcmerror = 0;

	/* length check on io buf */
	switch (vi->type) {
	case IOVT_BOOL:
	case IOVT_INT8:
	case IOVT_INT16:
	case IOVT_INT32:
	case IOVT_UINT8:
	case IOVT_UINT16:
	case IOVT_UINT32:
		/* all integers are s32 sized args at the ioctl interface */
		if (len < (int)sizeof(int)) {
			bcmerror = -EOVERFLOW;
		}
		break;

	case IOVT_BUFFER:
		/* buffer must meet minimum length requirement */
		if (len < vi->minlen) {
			bcmerror = -EOVERFLOW;
		}
		break;

	case IOVT_VOID:
		if (!set) {
			/* Cannot return nil... */
			bcmerror = -ENOTSUPP;
		} else if (len) {
			/* Set is an action w/o parameters */
			bcmerror = -ENOBUFS;
		}
		break;

	default:
		/* unknown type for length check in iovar info */
		bcmerror = -ENOTSUPP;
	}

	return bcmerror;
}

/*******************************************************************************
 * crc8
 *
 * Computes a crc8 over the input data using the polynomial:
 *
 *       x^8 + x^7 +x^6 + x^4 + x^2 + 1
 *
 * The caller provides the initial value (either CRC8_INIT_VALUE
 * or the previous returned value) to allow for processing of
 * discontiguous blocks of data.  When generating the CRC the
 * caller is responsible for complementing the final return value
 * and inserting it into the byte stream.  When checking, a final
 * return value of CRC8_GOOD_VALUE indicates a valid CRC.
 *
 * Reference: Dallas Semiconductor Application Note 27
 *   Williams, Ross N., "A Painless Guide to CRC Error Detection Algorithms",
 *     ver 3, Aug 1993, ross@guest.adelaide.edu.au, Rocksoft Pty Ltd.,
 *     ftp://ftp.rocksoft.com/clients/rocksoft/papers/crc_v3.txt
 *
 * ****************************************************************************
 */

static const u8 crc8_table[256] = {
	0x00, 0xF7, 0xB9, 0x4E, 0x25, 0xD2, 0x9C, 0x6B,
	0x4A, 0xBD, 0xF3, 0x04, 0x6F, 0x98, 0xD6, 0x21,
	0x94, 0x63, 0x2D, 0xDA, 0xB1, 0x46, 0x08, 0xFF,
	0xDE, 0x29, 0x67, 0x90, 0xFB, 0x0C, 0x42, 0xB5,
	0x7F, 0x88, 0xC6, 0x31, 0x5A, 0xAD, 0xE3, 0x14,
	0x35, 0xC2, 0x8C, 0x7B, 0x10, 0xE7, 0xA9, 0x5E,
	0xEB, 0x1C, 0x52, 0xA5, 0xCE, 0x39, 0x77, 0x80,
	0xA1, 0x56, 0x18, 0xEF, 0x84, 0x73, 0x3D, 0xCA,
	0xFE, 0x09, 0x47, 0xB0, 0xDB, 0x2C, 0x62, 0x95,
	0xB4, 0x43, 0x0D, 0xFA, 0x91, 0x66, 0x28, 0xDF,
	0x6A, 0x9D, 0xD3, 0x24, 0x4F, 0xB8, 0xF6, 0x01,
	0x20, 0xD7, 0x99, 0x6E, 0x05, 0xF2, 0xBC, 0x4B,
	0x81, 0x76, 0x38, 0xCF, 0xA4, 0x53, 0x1D, 0xEA,
	0xCB, 0x3C, 0x72, 0x85, 0xEE, 0x19, 0x57, 0xA0,
	0x15, 0xE2, 0xAC, 0x5B, 0x30, 0xC7, 0x89, 0x7E,
	0x5F, 0xA8, 0xE6, 0x11, 0x7A, 0x8D, 0xC3, 0x34,
	0xAB, 0x5C, 0x12, 0xE5, 0x8E, 0x79, 0x37, 0xC0,
	0xE1, 0x16, 0x58, 0xAF, 0xC4, 0x33, 0x7D, 0x8A,
	0x3F, 0xC8, 0x86, 0x71, 0x1A, 0xED, 0xA3, 0x54,
	0x75, 0x82, 0xCC, 0x3B, 0x50, 0xA7, 0xE9, 0x1E,
	0xD4, 0x23, 0x6D, 0x9A, 0xF1, 0x06, 0x48, 0xBF,
	0x9E, 0x69, 0x27, 0xD0, 0xBB, 0x4C, 0x02, 0xF5,
	0x40, 0xB7, 0xF9, 0x0E, 0x65, 0x92, 0xDC, 0x2B,
	0x0A, 0xFD, 0xB3, 0x44, 0x2F, 0xD8, 0x96, 0x61,
	0x55, 0xA2, 0xEC, 0x1B, 0x70, 0x87, 0xC9, 0x3E,
	0x1F, 0xE8, 0xA6, 0x51, 0x3A, 0xCD, 0x83, 0x74,
	0xC1, 0x36, 0x78, 0x8F, 0xE4, 0x13, 0x5D, 0xAA,
	0x8B, 0x7C, 0x32, 0xC5, 0xAE, 0x59, 0x17, 0xE0,
	0x2A, 0xDD, 0x93, 0x64, 0x0F, 0xF8, 0xB6, 0x41,
	0x60, 0x97, 0xD9, 0x2E, 0x45, 0xB2, 0xFC, 0x0B,
	0xBE, 0x49, 0x07, 0xF0, 0x9B, 0x6C, 0x22, 0xD5,
	0xF4, 0x03, 0x4D, 0xBA, 0xD1, 0x26, 0x68, 0x9F
};

#define CRC_INNER_LOOP(n, c, x) \
	((c) = ((c) >> 8) ^ crc##n##_table[((c) ^ (x)) & 0xff])

u8 hndcrc8(u8 *pdata,	/* pointer to array of data to process */
			 uint nbytes,	/* number of input data bytes to process */
			 u8 crc	/* either CRC8_INIT_VALUE or previous return value */
    ) {
	/* hard code the crc loop instead of using CRC_INNER_LOOP macro
	 * to avoid the undefined and unnecessary (u8 >> 8) operation.
	 */
	while (nbytes-- > 0)
		crc = crc8_table[(crc ^ *pdata++) & 0xff];

	return crc;
}

static const u32 crc32_table[256] = {
	0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
	0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
	0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
	0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
	0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
	0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
	0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
	0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
	0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
	0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
	0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
	0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
	0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
	0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
	0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
	0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
	0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
	0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
	0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
	0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
	0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
	0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
	0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
	0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
	0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
	0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
	0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
	0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
	0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
	0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
	0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
	0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
	0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
	0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
	0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
	0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
	0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
	0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
	0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
	0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
	0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
	0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
	0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
	0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
	0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
	0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
	0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
	0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
	0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
	0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
	0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
	0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
	0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
	0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
	0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
	0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
	0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
	0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
	0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
	0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
	0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
	0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
	0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
	0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

u32 hndcrc32(u8 *pdata,	/* pointer to array of data to process */
		uint nbytes,	/* number of input data bytes to process */
		u32 crc	/* either CRC32_INIT_VALUE or previous
					 return value */
)
{
	u8 *pend;
#ifdef __mips__
	u8 tmp[4];
	unsigned long *tptr = (unsigned long *) tmp;

	/* in case the beginning of the buffer isn't aligned */
	pend = (u8 *) ((uint) (pdata + 3) & 0xfffffffc);
	nbytes -= (pend - pdata);
	while (pdata < pend)
		CRC_INNER_LOOP(32, crc, *pdata++);

	/* handle bulk of data as 32-bit words */
	pend = pdata + (nbytes & 0xfffffffc);
	while (pdata < pend) {
		*tptr = *(unsigned long *) pdata;
		pdata += sizeof(unsigned long *);
		CRC_INNER_LOOP(32, crc, tmp[0]);
		CRC_INNER_LOOP(32, crc, tmp[1]);
		CRC_INNER_LOOP(32, crc, tmp[2]);
		CRC_INNER_LOOP(32, crc, tmp[3]);
	}

	/* 1-3 bytes at end of buffer */
	pend = pdata + (nbytes & 0x03);
	while (pdata < pend)
		CRC_INNER_LOOP(32, crc, *pdata++);
#else
	pend = pdata + nbytes;
	while (pdata < pend)
		CRC_INNER_LOOP(32, crc, *pdata++);
#endif				/* __mips__ */

	return crc;
}
/*
 * Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag
 */
bcm_tlv_t *bcm_parse_tlvs(void *buf, int buflen, uint key)
{
	bcm_tlv_t *elt;
	int totlen;

	elt = (bcm_tlv_t *) buf;
	totlen = buflen;

	/* find tagged parameter */
	while (totlen >= 2) {
		int len = elt->len;

		/* validate remaining totlen */
		if ((elt->id == key) && (totlen >= (len + 2)))
			return elt;

		elt = (bcm_tlv_t *) ((u8 *) elt + (len + 2));
		totlen -= (len + 2);
	}

	return NULL;
}


#if defined(BCMDBG)
int
bcm_format_flags(const bcm_bit_desc_t *bd, u32 flags, char *buf, int len)
{
	int i;
	char *p = buf;
	char hexstr[16];
	int slen = 0, nlen = 0;
	u32 bit;
	const char *name;

	if (len < 2 || !buf)
		return 0;

	buf[0] = '\0';

	for (i = 0; flags != 0; i++) {
		bit = bd[i].bit;
		name = bd[i].name;
		if (bit == 0 && flags != 0) {
			/* print any unnamed bits */
			snprintf(hexstr, 16, "0x%X", flags);
			name = hexstr;
			flags = 0;	/* exit loop */
		} else if ((flags & bit) == 0)
			continue;
		flags &= ~bit;
		nlen = strlen(name);
		slen += nlen;
		/* count btwn flag space */
		if (flags != 0)
			slen += 1;
		/* need NULL char as well */
		if (len <= slen)
			break;
		/* copy NULL char but don't count it */
		strncpy(p, name, nlen + 1);
		p += nlen;
		/* copy btwn flag space and NULL char */
		if (flags != 0)
			p += snprintf(p, 2, " ");
		len -= slen;
	}

	/* indicate the str was too short */
	if (flags != 0) {
		if (len < 2)
			p -= 2 - len;	/* overwrite last char */
		p += snprintf(p, 2, ">");
	}

	return (int)(p - buf);
}

/* print bytes formatted as hex to a string. return the resulting string length */
int bcm_format_hex(char *str, const void *bytes, int len)
{
	int i;
	char *p = str;
	const u8 *src = (const u8 *)bytes;

	for (i = 0; i < len; i++) {
		p += snprintf(p, 3, "%02X", *src);
		src++;
	}
	return (int)(p - str);
}
#endif				/* defined(BCMDBG) */

/* pretty hex print a contiguous buffer */
void prhex(const char *msg, unsigned char *buf, uint nbytes)
{
	char line[128], *p;
	int len = sizeof(line);
	int nchar;
	uint i;

	if (msg && (msg[0] != '\0'))
		printk(KERN_DEBUG "%s:\n", msg);

	p = line;
	for (i = 0; i < nbytes; i++) {
		if (i % 16 == 0) {
			nchar = snprintf(p, len, "  %04d: ", i);	/* line prefix */
			p += nchar;
			len -= nchar;
		}
		if (len > 0) {
			nchar = snprintf(p, len, "%02x ", buf[i]);
			p += nchar;
			len -= nchar;
		}

		if (i % 16 == 15) {
			printk(KERN_DEBUG "%s\n", line);	/* flush line */
			p = line;
			len = sizeof(line);
		}
	}

	/* flush last partial line */
	if (p != line)
		printk(KERN_DEBUG "%s\n", line);
}

char *bcm_chipname(uint chipid, char *buf, uint len)
{
	const char *fmt;

	fmt = ((chipid > 0xa000) || (chipid < 0x4000)) ? "%d" : "%x";
	snprintf(buf, len, fmt, chipid);
	return buf;
}

uint bcm_mkiovar(char *name, char *data, uint datalen, char *buf, uint buflen)
{
	uint len;

	len = strlen(name) + 1;

	if ((len + datalen) > buflen)
		return 0;

	strncpy(buf, name, buflen);

	/* append data onto the end of the name string */
	memcpy(&buf[len], data, datalen);
	len += datalen;

	return len;
}

/* Quarter dBm units to mW
 * Table starts at QDBM_OFFSET, so the first entry is mW for qdBm=153
 * Table is offset so the last entry is largest mW value that fits in
 * a u16.
 */

#define QDBM_OFFSET 153		/* Offset for first entry */
#define QDBM_TABLE_LEN 40	/* Table size */

/* Smallest mW value that will round up to the first table entry, QDBM_OFFSET.
 * Value is ( mW(QDBM_OFFSET - 1) + mW(QDBM_OFFSET) ) / 2
 */
#define QDBM_TABLE_LOW_BOUND 6493	/* Low bound */

/* Largest mW value that will round down to the last table entry,
 * QDBM_OFFSET + QDBM_TABLE_LEN-1.
 * Value is ( mW(QDBM_OFFSET + QDBM_TABLE_LEN - 1) +
 * mW(QDBM_OFFSET + QDBM_TABLE_LEN) ) / 2.
 */
#define QDBM_TABLE_HIGH_BOUND 64938	/* High bound */

static const u16 nqdBm_to_mW_map[QDBM_TABLE_LEN] = {
/* qdBm: 	+0 	+1 	+2 	+3 	+4 	+5 	+6 	+7 */
/* 153: */ 6683, 7079, 7499, 7943, 8414, 8913, 9441, 10000,
/* 161: */ 10593, 11220, 11885, 12589, 13335, 14125, 14962, 15849,
/* 169: */ 16788, 17783, 18836, 19953, 21135, 22387, 23714, 25119,
/* 177: */ 26607, 28184, 29854, 31623, 33497, 35481, 37584, 39811,
/* 185: */ 42170, 44668, 47315, 50119, 53088, 56234, 59566, 63096
};

u16 bcm_qdbm_to_mw(u8 qdbm)
{
	uint factor = 1;
	int idx = qdbm - QDBM_OFFSET;

	if (idx >= QDBM_TABLE_LEN) {
		/* clamp to max u16 mW value */
		return 0xFFFF;
	}

	/* scale the qdBm index up to the range of the table 0-40
	 * where an offset of 40 qdBm equals a factor of 10 mW.
	 */
	while (idx < 0) {
		idx += 40;
		factor *= 10;
	}

	/* return the mW value scaled down to the correct factor of 10,
	 * adding in factor/2 to get proper rounding.
	 */
	return (nqdBm_to_mW_map[idx] + factor / 2) / factor;
}
u8 bcm_mw_to_qdbm(u16 mw)
{
	u8 qdbm;
	int offset;
	uint mw_uint = mw;
	uint boundary;

	/* handle boundary case */
	if (mw_uint <= 1)
		return 0;

	offset = QDBM_OFFSET;

	/* move mw into the range of the table */
	while (mw_uint < QDBM_TABLE_LOW_BOUND) {
		mw_uint *= 10;
		offset -= 40;
	}

	for (qdbm = 0; qdbm < QDBM_TABLE_LEN - 1; qdbm++) {
		boundary = nqdBm_to_mW_map[qdbm] + (nqdBm_to_mW_map[qdbm + 1] -
						    nqdBm_to_mW_map[qdbm]) / 2;
		if (mw_uint < boundary)
			break;
	}

	qdbm += (u8) offset;

	return qdbm;
}
uint bcm_bitcount(u8 *bitmap, uint length)
{
	uint bitcount = 0, i;
	u8 tmp;
	for (i = 0; i < length; i++) {
		tmp = bitmap[i];
		while (tmp) {
			bitcount++;
			tmp &= (tmp - 1);
		}
	}
	return bitcount;
}
/* Initialization of bcmstrbuf structure */
void bcm_binit(struct bcmstrbuf *b, char *buf, uint size)
{
	b->origsize = b->size = size;
	b->origbuf = b->buf = buf;
}

/* Buffer sprintf wrapper to guard against buffer overflow */
int bcm_bprintf(struct bcmstrbuf *b, const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vsnprintf(b->buf, b->size, fmt, ap);

	/* Non Ansi C99 compliant returns -1,
	 * Ansi compliant return r >= b->size,
	 * bcmstdlib returns 0, handle all
	 */
	if ((r == -1) || (r >= (int)b->size) || (r == 0)) {
		b->size = 0;
	} else {
		b->size -= r;
		b->buf += r;
	}

	va_end(ap);

	return r;
}
