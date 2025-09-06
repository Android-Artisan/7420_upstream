/*
 * Copyright (C)2006 USAGI/WIDE Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author:
 * 	Kazunori Miyazawa <miyazawa@linux-ipv6.org>
 */

#include <crypto/scatterwalk.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/hardirq.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <crypto/internal/hash.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>

static u_int32_t ks[12] = {0x01010101, 0x01010101, 0x01010101, 0x01010101,
			   0x02020202, 0x02020202, 0x02020202, 0x02020202,
			   0x03030303, 0x03030303, 0x03030303, 0x03030303};

/*
 * +------------------------
 * | <parent tfm>
 * +------------------------
 * | crypto_xcbc_ctx
 * | xcbc_tfm_ctx
 * +------------------------
 * | consts (block size * 2)
 * +------------------------
 */
struct xcbc_tfm_ctx {
	struct crypto_cipher *child;
	u8 ctx[];
};

/*
 * +------------------------
 * | <shash desc>
 * +------------------------
 * | xcbc_desc_ctx
 * +------------------------
 * | odds (block size)
 * +------------------------
 * | prev (block size)
 * +------------------------
 * | key (block size)
 * +------------------------
 * | consts (block size * 3)
 * +------------------------
 */
struct crypto_xcbc_ctx {
	struct crypto_cipher *child;
	u8 *odds;
	u8 *prev;
	u8 *key;
	u8 *consts;
	void (*xor)(u8 *a, const u8 *b, unsigned int bs);
	unsigned int keylen;
	unsigned int len;
};

static void xor_128(u8 *a, const u8 *b, unsigned int bs)
{
	((u32 *)a)[0] ^= ((u32 *)b)[0];
	((u32 *)a)[1] ^= ((u32 *)b)[1];
	((u32 *)a)[2] ^= ((u32 *)b)[2];
	((u32 *)a)[3] ^= ((u32 *)b)[3];
}

static int _crypto_xcbc_digest_setkey(struct crypto_hash *parent,
				      struct crypto_xcbc_ctx *ctx)
{
	int bs = crypto_hash_blocksize(parent);
	int err = 0;
	u8 key1[bs];

	if ((err = crypto_cipher_setkey(ctx->child, ctx->key, ctx->keylen)))
	    return err;

	crypto_cipher_encrypt_one(ctx->child, key1, ctx->consts);

	return crypto_cipher_setkey(ctx->child, key1, bs);
}

static int crypto_xcbc_digest_setkey(struct crypto_hash *parent,
				     const u8 *inkey, unsigned int keylen)
{
	struct crypto_xcbc_ctx *ctx = crypto_hash_ctx_aligned(parent);

	if (keylen != crypto_cipher_blocksize(ctx->child))
		return -EINVAL;

	ctx->keylen = keylen;
	memcpy(ctx->key, inkey, keylen);
	ctx->consts = (u8*)ks;

	return _crypto_xcbc_digest_setkey(parent, ctx);
}

static int crypto_xcbc_digest_init(struct hash_desc *pdesc)
{
	struct crypto_xcbc_ctx *ctx = crypto_hash_ctx_aligned(pdesc->tfm);
	int bs = crypto_hash_blocksize(pdesc->tfm);

	ctx->len = 0;
	memset(ctx->odds, 0, bs);
	memset(ctx->prev, 0, bs);
 */
struct xcbc_desc_ctx {
	unsigned int len;
	u8 ctx[];
};

static int crypto_xcbc_digest_setkey(struct crypto_shash *parent,
				     const u8 *inkey, unsigned int keylen)
{
	unsigned long alignmask = crypto_shash_alignmask(parent);
	struct xcbc_tfm_ctx *ctx = crypto_shash_ctx(parent);
	int bs = crypto_shash_blocksize(parent);
	u8 *consts = PTR_ALIGN(&ctx->ctx[0], alignmask + 1);
	int err = 0;
	u8 key1[bs];

	if ((err = crypto_cipher_setkey(ctx->child, inkey, keylen)))
		return err;

	crypto_cipher_encrypt_one(ctx->child, consts, (u8 *)ks + bs);
	crypto_cipher_encrypt_one(ctx->child, consts + bs, (u8 *)ks + bs * 2);
	crypto_cipher_encrypt_one(ctx->child, key1, (u8 *)ks);

	return crypto_cipher_setkey(ctx->child, key1, bs);

}

static int crypto_xcbc_digest_init(struct shash_desc *pdesc)
{
	unsigned long alignmask = crypto_shash_alignmask(pdesc->tfm);
	struct xcbc_desc_ctx *ctx = shash_desc_ctx(pdesc);
	int bs = crypto_shash_blocksize(pdesc->tfm);
	u8 *prev = PTR_ALIGN(&ctx->ctx[0], alignmask + 1) + bs;

	ctx->len = 0;
	memset(prev, 0, bs);

	return 0;
}

static int crypto_xcbc_digest_update2(struct hash_desc *pdesc,
				      struct scatterlist *sg,
				      unsigned int nbytes)
{
	struct crypto_hash *parent = pdesc->tfm;
	struct crypto_xcbc_ctx *ctx = crypto_hash_ctx_aligned(parent);
	struct crypto_cipher *tfm = ctx->child;
	int bs = crypto_hash_blocksize(parent);

	for (;;) {
		struct page *pg = sg_page(sg);
		unsigned int offset = sg->offset;
		unsigned int slen = sg->length;

		if (unlikely(slen > nbytes))
			slen = nbytes;

		nbytes -= slen;

		while (slen > 0) {
			unsigned int len = min(slen, ((unsigned int)(PAGE_SIZE)) - offset);
			char *p = crypto_kmap(pg, 0) + offset;

			/* checking the data can fill the block */
			if ((ctx->len + len) <= bs) {
				memcpy(ctx->odds + ctx->len, p, len);
				ctx->len += len;
				slen -= len;

				/* checking the rest of the page */
				if (len + offset >= PAGE_SIZE) {
					offset = 0;
					pg++;
				} else
					offset += len;

				crypto_kunmap(p, 0);
				crypto_yield(pdesc->flags);
				continue;
			}

			/* filling odds with new data and encrypting it */
			memcpy(ctx->odds + ctx->len, p, bs - ctx->len);
			len -= bs - ctx->len;
			p += bs - ctx->len;

			ctx->xor(ctx->prev, ctx->odds, bs);
			crypto_cipher_encrypt_one(tfm, ctx->prev, ctx->prev);

			/* clearing the length */
			ctx->len = 0;

			/* encrypting the rest of data */
			while (len > bs) {
				ctx->xor(ctx->prev, p, bs);
				crypto_cipher_encrypt_one(tfm, ctx->prev,
							  ctx->prev);
				p += bs;
				len -= bs;
			}

			/* keeping the surplus of blocksize */
			if (len) {
				memcpy(ctx->odds, p, len);
				ctx->len = len;
			}
			crypto_kunmap(p, 0);
			crypto_yield(pdesc->flags);
			slen -= min(slen, ((unsigned int)(PAGE_SIZE)) - offset);
			offset = 0;
			pg++;
		}

		if (!nbytes)
			break;
		sg = scatterwalk_sg_next(sg);
static int crypto_xcbc_digest_update(struct shash_desc *pdesc, const u8 *p,
				     unsigned int len)
{
	struct crypto_shash *parent = pdesc->tfm;
	unsigned long alignmask = crypto_shash_alignmask(parent);
	struct xcbc_tfm_ctx *tctx = crypto_shash_ctx(parent);
	struct xcbc_desc_ctx *ctx = shash_desc_ctx(pdesc);
	struct crypto_cipher *tfm = tctx->child;
	int bs = crypto_shash_blocksize(parent);
	u8 *odds = PTR_ALIGN(&ctx->ctx[0], alignmask + 1);
	u8 *prev = odds + bs;

	/* checking the data can fill the block */
	if ((ctx->len + len) <= bs) {
		memcpy(odds + ctx->len, p, len);
		ctx->len += len;
		return 0;
	}

	/* filling odds with new data and encrypting it */
	memcpy(odds + ctx->len, p, bs - ctx->len);
	len -= bs - ctx->len;
	p += bs - ctx->len;

	crypto_xor(prev, odds, bs);
	crypto_cipher_encrypt_one(tfm, prev, prev);

	/* clearing the length */
	ctx->len = 0;

	/* encrypting the rest of data */
	while (len > bs) {
		crypto_xor(prev, p, bs);
		crypto_cipher_encrypt_one(tfm, prev, prev);
		p += bs;
		len -= bs;
	}

	/* keeping the surplus of blocksize */
	if (len) {
		memcpy(odds, p, len);
		ctx->len = len;
	}

	return 0;
}

static int crypto_xcbc_digest_update(struct hash_desc *pdesc,
				     struct scatterlist *sg,
				     unsigned int nbytes)
{
	if (WARN_ON_ONCE(in_irq()))
		return -EDEADLK;
	return crypto_xcbc_digest_update2(pdesc, sg, nbytes);
}

static int crypto_xcbc_digest_final(struct hash_desc *pdesc, u8 *out)
{
	struct crypto_hash *parent = pdesc->tfm;
	struct crypto_xcbc_ctx *ctx = crypto_hash_ctx_aligned(parent);
	struct crypto_cipher *tfm = ctx->child;
	int bs = crypto_hash_blocksize(parent);
	int err = 0;

	if (ctx->len == bs) {
		u8 key2[bs];

		if ((err = crypto_cipher_setkey(tfm, ctx->key, ctx->keylen)) != 0)
			return err;

		crypto_cipher_encrypt_one(tfm, key2,
					  (u8 *)(ctx->consts + bs));

		ctx->xor(ctx->prev, ctx->odds, bs);
		ctx->xor(ctx->prev, key2, bs);
		_crypto_xcbc_digest_setkey(parent, ctx);

		crypto_cipher_encrypt_one(tfm, out, ctx->prev);
	} else {
		u8 key3[bs];
		unsigned int rlen;
		u8 *p = ctx->odds + ctx->len;
static int crypto_xcbc_digest_final(struct shash_desc *pdesc, u8 *out)
{
	struct crypto_shash *parent = pdesc->tfm;
	unsigned long alignmask = crypto_shash_alignmask(parent);
	struct xcbc_tfm_ctx *tctx = crypto_shash_ctx(parent);
	struct xcbc_desc_ctx *ctx = shash_desc_ctx(pdesc);
	struct crypto_cipher *tfm = tctx->child;
	int bs = crypto_shash_blocksize(parent);
	u8 *consts = PTR_ALIGN(&tctx->ctx[0], alignmask + 1);
	u8 *odds = PTR_ALIGN(&ctx->ctx[0], alignmask + 1);
	u8 *prev = odds + bs;
	unsigned int offset = 0;

	if (ctx->len != bs) {
		unsigned int rlen;
		u8 *p = odds + ctx->len;

		*p = 0x80;
		p++;

		rlen = bs - ctx->len -1;
		if (rlen)
			memset(p, 0, rlen);

		if ((err = crypto_cipher_setkey(tfm, ctx->key, ctx->keylen)) != 0)
			return err;

		crypto_cipher_encrypt_one(tfm, key3,
					  (u8 *)(ctx->consts + bs * 2));

		ctx->xor(ctx->prev, ctx->odds, bs);
		ctx->xor(ctx->prev, key3, bs);

		_crypto_xcbc_digest_setkey(parent, ctx);

		crypto_cipher_encrypt_one(tfm, out, ctx->prev);
	}

	return 0;
}

static int crypto_xcbc_digest(struct hash_desc *pdesc,
		  struct scatterlist *sg, unsigned int nbytes, u8 *out)
{
	if (WARN_ON_ONCE(in_irq()))
		return -EDEADLK;

	crypto_xcbc_digest_init(pdesc);
	crypto_xcbc_digest_update2(pdesc, sg, nbytes);
	return crypto_xcbc_digest_final(pdesc, out);
}

		offset += bs;
	}

	crypto_xor(prev, odds, bs);
	crypto_xor(prev, consts + offset, bs);

	crypto_cipher_encrypt_one(tfm, out, prev);

	return 0;
}

static int xcbc_init_tfm(struct crypto_tfm *tfm)
{
	struct crypto_cipher *cipher;
	struct crypto_instance *inst = (void *)tfm->__crt_alg;
	struct crypto_spawn *spawn = crypto_instance_ctx(inst);
	struct crypto_xcbc_ctx *ctx = crypto_hash_ctx_aligned(__crypto_hash_cast(tfm));
	int bs = crypto_hash_blocksize(__crypto_hash_cast(tfm));
	struct xcbc_tfm_ctx *ctx = crypto_tfm_ctx(tfm);

	cipher = crypto_spawn_cipher(spawn);
	if (IS_ERR(cipher))
		return PTR_ERR(cipher);

	switch(bs) {
	case 16:
		ctx->xor = xor_128;
		break;
	default:
		return -EINVAL;
	}

	ctx->child = cipher;
	ctx->odds = (u8*)(ctx+1);
	ctx->prev = ctx->odds + bs;
	ctx->key = ctx->prev + bs;
	ctx->child = cipher;

	return 0;
};

static void xcbc_exit_tfm(struct crypto_tfm *tfm)
{
	struct crypto_xcbc_ctx *ctx = crypto_hash_ctx_aligned(__crypto_hash_cast(tfm));
	crypto_free_cipher(ctx->child);
}

static struct crypto_instance *xcbc_alloc(struct rtattr **tb)
{
	struct crypto_instance *inst;
	struct crypto_alg *alg;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_HASH);
	if (err)
		return ERR_PTR(err);
	struct xcbc_tfm_ctx *ctx = crypto_tfm_ctx(tfm);
	crypto_free_cipher(ctx->child);
}

static int xcbc_create(struct crypto_template *tmpl, struct rtattr **tb)
{
	struct shash_instance *inst;
	struct crypto_alg *alg;
	unsigned long alignmask;
	int err;

	err = crypto_check_attr_type(tb, CRYPTO_ALG_TYPE_SHASH);
	if (err)
		return err;

	alg = crypto_get_attr_alg(tb, CRYPTO_ALG_TYPE_CIPHER,
				  CRYPTO_ALG_TYPE_MASK);
	if (IS_ERR(alg))
		return ERR_CAST(alg);
		return PTR_ERR(alg);

	switch(alg->cra_blocksize) {
	case 16:
		break;
	default:
		inst = ERR_PTR(-EINVAL);
		goto out_put_alg;
	}

	inst = crypto_alloc_instance("xcbc", alg);
	if (IS_ERR(inst))
		goto out_put_alg;

	inst->alg.cra_flags = CRYPTO_ALG_TYPE_HASH;
	inst->alg.cra_priority = alg->cra_priority;
	inst->alg.cra_blocksize = alg->cra_blocksize;
	inst->alg.cra_alignmask = alg->cra_alignmask;
	inst->alg.cra_type = &crypto_hash_type;

	inst->alg.cra_hash.digestsize = alg->cra_blocksize;
	inst->alg.cra_ctxsize = sizeof(struct crypto_xcbc_ctx) +
				ALIGN(inst->alg.cra_blocksize * 3, sizeof(void *));
	inst->alg.cra_init = xcbc_init_tfm;
	inst->alg.cra_exit = xcbc_exit_tfm;

	inst->alg.cra_hash.init = crypto_xcbc_digest_init;
	inst->alg.cra_hash.update = crypto_xcbc_digest_update;
	inst->alg.cra_hash.final = crypto_xcbc_digest_final;
	inst->alg.cra_hash.digest = crypto_xcbc_digest;
	inst->alg.cra_hash.setkey = crypto_xcbc_digest_setkey;

out_put_alg:
	crypto_mod_put(alg);
	return inst;
}

static void xcbc_free(struct crypto_instance *inst)
{
	crypto_drop_spawn(crypto_instance_ctx(inst));
	kfree(inst);
		goto out_put_alg;
	}

	inst = shash_alloc_instance("xcbc", alg);
	err = PTR_ERR(inst);
	if (IS_ERR(inst))
		goto out_put_alg;

	err = crypto_init_spawn(shash_instance_ctx(inst), alg,
				shash_crypto_instance(inst),
				CRYPTO_ALG_TYPE_MASK);
	if (err)
		goto out_free_inst;

	alignmask = alg->cra_alignmask | 3;
	inst->alg.base.cra_alignmask = alignmask;
	inst->alg.base.cra_priority = alg->cra_priority;
	inst->alg.base.cra_blocksize = alg->cra_blocksize;

	inst->alg.digestsize = alg->cra_blocksize;
	inst->alg.descsize = ALIGN(sizeof(struct xcbc_desc_ctx),
				   crypto_tfm_ctx_alignment()) +
			     (alignmask &
			      ~(crypto_tfm_ctx_alignment() - 1)) +
			     alg->cra_blocksize * 2;

	inst->alg.base.cra_ctxsize = ALIGN(sizeof(struct xcbc_tfm_ctx),
					   alignmask + 1) +
				     alg->cra_blocksize * 2;
	inst->alg.base.cra_init = xcbc_init_tfm;
	inst->alg.base.cra_exit = xcbc_exit_tfm;

	inst->alg.init = crypto_xcbc_digest_init;
	inst->alg.update = crypto_xcbc_digest_update;
	inst->alg.final = crypto_xcbc_digest_final;
	inst->alg.setkey = crypto_xcbc_digest_setkey;

	err = shash_register_instance(tmpl, inst);
	if (err) {
out_free_inst:
		shash_free_instance(shash_crypto_instance(inst));
	}

out_put_alg:
	crypto_mod_put(alg);
	return err;
}

static struct crypto_template crypto_xcbc_tmpl = {
	.name = "xcbc",
	.alloc = xcbc_alloc,
	.free = xcbc_free,
	.create = xcbc_create,
	.free = shash_free_instance,
	.module = THIS_MODULE,
};

static int __init crypto_xcbc_module_init(void)
{
	return crypto_register_template(&crypto_xcbc_tmpl);
}

static void __exit crypto_xcbc_module_exit(void)
{
	crypto_unregister_template(&crypto_xcbc_tmpl);
}

module_init(crypto_xcbc_module_init);
module_exit(crypto_xcbc_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("XCBC keyed hash algorithm");
MODULE_ALIAS_CRYPTO("xcbc");
