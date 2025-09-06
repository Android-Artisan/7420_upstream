/*
 * Quick & dirty crypto testing module.
 *
 * This will only exist until we have a better testing mechanism
 * (e.g. a char device).
 *
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2002 Jean-Francois Dive <jef@linuxbe.org>
 * Copyright (c) 2007 Nokia Siemens Networks
 *
 * Updated RFC4106 AES-GCM testing.
 *    Authors: Aidan O'Mahony (aidan.o.mahony@intel.com)
 *             Adrian Hoban <adrian.hoban@intel.com>
 *             Gabriele Paoloni <gabriele.paoloni@intel.com>
 *             Tadeusz Struk (tadeusz.struk@intel.com)
 *             Copyright (c) 2010, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/hash.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <linux/crypto.h>
#include <crypto/aead.h>
#include <crypto/hash.h>
#include <linux/err.h>
#include <linux/fips.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <linux/moduleparam.h>
#include <linux/jiffies.h>
#include <linux/timex.h>
#include <linux/interrupt.h>
#include "tcrypt.h"

/*
 * Need to kmalloc() memory for testing.
 */
#define TVMEMSIZE	16384
#define XBUFSIZE	32768

/*
 * Indexes into the xbuf to simulate cross-page access.
 */
#define IDX1		32
#define IDX2		32400
#define IDX3		1
#define IDX4		8193
#define IDX5		22222
#define IDX6		17101
#define IDX7		27333
#define IDX8		3000

/*
* Used by test_cipher()
 * Need slab memory for testing (size in number of pages).
 */
#define TVMEMSIZE	4

/*
* Used by test_cipher_speed()
*/
#define ENCRYPT 1
#define DECRYPT 0

struct tcrypt_result {
	struct completion completion;
	int err;
};

static unsigned int IDX[8] = { IDX1, IDX2, IDX3, IDX4, IDX5, IDX6, IDX7, IDX8 };
#define MAX_DIGEST_SIZE		64

/*
 * return a string with the driver name
 */
#define get_driver_name(tfm_type, tfm) crypto_tfm_alg_driver_name(tfm_type ## _tfm(tfm))

/*
 * Used by test_cipher_speed()
 */
static unsigned int sec;

static int mode;
static char *xbuf;
static char *axbuf;
static char *tvmem;
static char *alg = NULL;
static u32 type;
static u32 mask;
static int mode;
static char *tvmem[TVMEMSIZE];

static char *check[] = {
	"des", "md5", "des3_ede", "rot13", "sha1", "sha224", "sha256",
	"blowfish", "twofish", "serpent", "sha384", "sha512", "md4", "aes",
	"cast6", "arc4", "michael_mic", "deflate", "crc32c", "tea", "xtea",
	"khazad", "wp512", "wp384", "wp256", "tnepres", "xeta",  "fcrypt",
	"camellia", "seed", "salsa20", "rmd128", "rmd160", "rmd256", "rmd320",
	"lzo", "cts", NULL
};

static void hexdump(unsigned char *buf, unsigned int len)
{
	print_hex_dump(KERN_CONT, "", DUMP_PREFIX_OFFSET,
			16, 1,
			buf, len, false);
}
	"lzo", "cts", "zlib", NULL
};

struct tcrypt_result {
	struct completion completion;
	int err;
};

static void tcrypt_complete(struct crypto_async_request *req, int err)
{
	struct tcrypt_result *res = req->data;

	if (err == -EINPROGRESS)
		return;

	res->err = err;
	complete(&res->completion);
}

static void test_hash(char *algo, struct hash_testvec *template,
		      unsigned int tcount)
{
	unsigned int i, j, k, temp;
	struct scatterlist sg[8];
	char result[64];
	struct crypto_ahash *tfm;
	struct ahash_request *req;
	struct tcrypt_result tresult;
	int ret;
	void *hash_buff;

	printk("\ntesting %s\n", algo);

	init_completion(&tresult.completion);

	tfm = crypto_alloc_ahash(algo, 0, 0);
	if (IS_ERR(tfm)) {
		printk("failed to load transform for %s: %ld\n", algo,
		       PTR_ERR(tfm));
		return;
	}

	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		printk(KERN_ERR "failed to allocate request for %s\n", algo);
		goto out_noreq;
	}
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tcrypt_complete, &tresult);

	for (i = 0; i < tcount; i++) {
		printk("test %u:\n", i + 1);
		memset(result, 0, 64);

		hash_buff = kzalloc(template[i].psize, GFP_KERNEL);
		if (!hash_buff)
			continue;

		memcpy(hash_buff, template[i].plaintext, template[i].psize);
		sg_init_one(&sg[0], hash_buff, template[i].psize);

		if (template[i].ksize) {
			crypto_ahash_clear_flags(tfm, ~0);
			ret = crypto_ahash_setkey(tfm, template[i].key,
						  template[i].ksize);
			if (ret) {
				printk("setkey() failed ret=%d\n", ret);
				kfree(hash_buff);
				goto out;
			}
		}

		ahash_request_set_crypt(req, sg, result, template[i].psize);
		ret = crypto_ahash_digest(req);
		switch (ret) {
		case 0:
			break;
		case -EINPROGRESS:
		case -EBUSY:
			ret = wait_for_completion_interruptible(
				&tresult.completion);
			if (!ret && !(ret = tresult.err)) {
				INIT_COMPLETION(tresult.completion);
				break;
			}
			/* fall through */
		default:
			printk("digest () failed ret=%d\n", ret);
			kfree(hash_buff);
			goto out;
		}

		hexdump(result, crypto_ahash_digestsize(tfm));
		printk("%s\n",
		       memcmp(result, template[i].digest,
			      crypto_ahash_digestsize(tfm)) ?
		       "fail" : "pass");
		kfree(hash_buff);
	}

	printk("testing %s across pages\n", algo);

	/* setup the dummy buffer first */
	memset(xbuf, 0, XBUFSIZE);

	j = 0;
	for (i = 0; i < tcount; i++) {
		if (template[i].np) {
			j++;
			printk("test %u:\n", j);
			memset(result, 0, 64);

			temp = 0;
			sg_init_table(sg, template[i].np);
			for (k = 0; k < template[i].np; k++) {
				memcpy(&xbuf[IDX[k]],
				       template[i].plaintext + temp,
				       template[i].tap[k]);
				temp += template[i].tap[k];
				sg_set_buf(&sg[k], &xbuf[IDX[k]],
					    template[i].tap[k]);
			}

			if (template[i].ksize) {
				crypto_ahash_clear_flags(tfm, ~0);
				ret = crypto_ahash_setkey(tfm, template[i].key,
							  template[i].ksize);

				if (ret) {
					printk("setkey() failed ret=%d\n", ret);
					goto out;
				}
			}

			ahash_request_set_crypt(req, sg, result,
						template[i].psize);
			ret = crypto_ahash_digest(req);
			switch (ret) {
			case 0:
				break;
			case -EINPROGRESS:
			case -EBUSY:
				ret = wait_for_completion_interruptible(
					&tresult.completion);
				if (!ret && !(ret = tresult.err)) {
					INIT_COMPLETION(tresult.completion);
					break;
				}
				/* fall through */
			default:
				printk("digest () failed ret=%d\n", ret);
				goto out;
			}

			hexdump(result, crypto_ahash_digestsize(tfm));
			printk("%s\n",
			       memcmp(result, template[i].digest,
				      crypto_ahash_digestsize(tfm)) ?
			       "fail" : "pass");
		}
	}

out:
	ahash_request_free(req);
out_noreq:
	crypto_free_ahash(tfm);
}

static void test_aead(char *algo, int enc, struct aead_testvec *template,
		      unsigned int tcount)
{
	unsigned int ret, i, j, k, n, temp;
	char *q;
	struct crypto_aead *tfm;
	char *key;
	struct aead_request *req;
	struct scatterlist sg[8];
	struct scatterlist asg[8];
	const char *e;
	struct tcrypt_result result;
	unsigned int authsize;
	void *input;
	void *assoc;
	char iv[MAX_IVLEN];

	if (enc == ENCRYPT)
		e = "encryption";
	else
		e = "decryption";

	printk(KERN_INFO "\ntesting %s %s\n", algo, e);

	init_completion(&result.completion);

	tfm = crypto_alloc_aead(algo, 0, 0);

	if (IS_ERR(tfm)) {
		printk(KERN_INFO "failed to load transform for %s: %ld\n",
		       algo, PTR_ERR(tfm));
		return;
	}

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		printk(KERN_INFO "failed to allocate request for %s\n", algo);
		goto out;
	}

	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  tcrypt_complete, &result);

	for (i = 0, j = 0; i < tcount; i++) {
		if (!template[i].np) {
			printk(KERN_INFO "test %u (%d bit key):\n",
			       ++j, template[i].klen * 8);

			/* some tepmplates have no input data but they will
			 * touch input
			 */
			input = kzalloc(template[i].ilen + template[i].rlen, GFP_KERNEL);
			if (!input)
				continue;

			assoc = kzalloc(template[i].alen, GFP_KERNEL);
			if (!assoc) {
				kfree(input);
				continue;
			}

			memcpy(input, template[i].input, template[i].ilen);
			memcpy(assoc, template[i].assoc, template[i].alen);
			if (template[i].iv)
				memcpy(iv, template[i].iv, MAX_IVLEN);
			else
				memset(iv, 0, MAX_IVLEN);

			crypto_aead_clear_flags(tfm, ~0);
			if (template[i].wk)
				crypto_aead_set_flags(
					tfm, CRYPTO_TFM_REQ_WEAK_KEY);

			if (template[i].key)
				key = template[i].key;
			else
				key = kzalloc(template[i].klen, GFP_KERNEL);

			ret = crypto_aead_setkey(tfm, key,
						 template[i].klen);
			if (ret) {
				printk(KERN_INFO "setkey() failed flags=%x\n",
				       crypto_aead_get_flags(tfm));

				if (!template[i].fail)
					goto next_one;
			}

			authsize = abs(template[i].rlen - template[i].ilen);
			ret = crypto_aead_setauthsize(tfm, authsize);
			if (ret) {
				printk(KERN_INFO
				       "failed to set authsize = %u\n",
				       authsize);
				goto next_one;
			}

			sg_init_one(&sg[0], input,
				    template[i].ilen + (enc ? authsize : 0));

			sg_init_one(&asg[0], assoc, template[i].alen);

			aead_request_set_crypt(req, sg, sg,
					       template[i].ilen, iv);

			aead_request_set_assoc(req, asg, template[i].alen);

			ret = enc ?
				crypto_aead_encrypt(req) :
				crypto_aead_decrypt(req);

			switch (ret) {
			case 0:
				break;
			case -EINPROGRESS:
			case -EBUSY:
				ret = wait_for_completion_interruptible(
					&result.completion);
				if (!ret && !(ret = result.err)) {
					INIT_COMPLETION(result.completion);
					break;
				}
				/* fall through */
			default:
				printk(KERN_INFO "%s () failed err=%d\n",
				       e, -ret);
				goto next_one;
			}

			q = input;
			hexdump(q, template[i].rlen);

			printk(KERN_INFO "enc/dec: %s\n",
			       memcmp(q, template[i].result,
				      template[i].rlen) ? "fail" : "pass");
next_one:
			if (!template[i].key)
				kfree(key);
			kfree(assoc);
			kfree(input);
		}
	}

	printk(KERN_INFO "\ntesting %s %s across pages (chunking)\n", algo, e);
	memset(axbuf, 0, XBUFSIZE);

	for (i = 0, j = 0; i < tcount; i++) {
		if (template[i].np) {
			printk(KERN_INFO "test %u (%d bit key):\n",
			       ++j, template[i].klen * 8);

			if (template[i].iv)
				memcpy(iv, template[i].iv, MAX_IVLEN);
			else
				memset(iv, 0, MAX_IVLEN);

			crypto_aead_clear_flags(tfm, ~0);
			if (template[i].wk)
				crypto_aead_set_flags(
					tfm, CRYPTO_TFM_REQ_WEAK_KEY);
			key = template[i].key;

			ret = crypto_aead_setkey(tfm, key, template[i].klen);
			if (ret) {
				printk(KERN_INFO "setkey() failed flags=%x\n",
				       crypto_aead_get_flags(tfm));

				if (!template[i].fail)
					goto out;
			}

			memset(xbuf, 0, XBUFSIZE);
			sg_init_table(sg, template[i].np);
			for (k = 0, temp = 0; k < template[i].np; k++) {
				memcpy(&xbuf[IDX[k]],
				       template[i].input + temp,
				       template[i].tap[k]);
				temp += template[i].tap[k];
				sg_set_buf(&sg[k], &xbuf[IDX[k]],
					   template[i].tap[k]);
			}

			authsize = abs(template[i].rlen - template[i].ilen);
			ret = crypto_aead_setauthsize(tfm, authsize);
			if (ret) {
				printk(KERN_INFO
				       "failed to set authsize = %u\n",
				       authsize);
				goto out;
			}

			if (enc)
				sg[k - 1].length += authsize;

			sg_init_table(asg, template[i].anp);
			for (k = 0, temp = 0; k < template[i].anp; k++) {
				memcpy(&axbuf[IDX[k]],
				       template[i].assoc + temp,
				       template[i].atap[k]);
				temp += template[i].atap[k];
				sg_set_buf(&asg[k], &axbuf[IDX[k]],
					   template[i].atap[k]);
			}

			aead_request_set_crypt(req, sg, sg,
					       template[i].ilen,
					       iv);

			aead_request_set_assoc(req, asg, template[i].alen);

			ret = enc ?
				crypto_aead_encrypt(req) :
				crypto_aead_decrypt(req);

			switch (ret) {
			case 0:
				break;
			case -EINPROGRESS:
			case -EBUSY:
				ret = wait_for_completion_interruptible(
					&result.completion);
				if (!ret && !(ret = result.err)) {
					INIT_COMPLETION(result.completion);
					break;
				}
				/* fall through */
			default:
				printk(KERN_INFO "%s () failed err=%d\n",
				       e, -ret);
				goto out;
			}

			for (k = 0, temp = 0; k < template[i].np; k++) {
				printk(KERN_INFO "page %u\n", k);
				q = &xbuf[IDX[k]];

				n = template[i].tap[k];
				if (k == template[i].np - 1)
					n += enc ? authsize : -authsize;
				hexdump(q, n);
				printk(KERN_INFO "%s\n",
				       memcmp(q, template[i].result + temp, n) ?
				       "fail" : "pass");

				q += n;
				if (k == template[i].np - 1 && !enc) {
					if (memcmp(q, template[i].input +
						      temp + n, authsize))
						n = authsize;
					else
						n = 0;
				} else {
					for (n = 0; q[n]; n++)
						;
				}
				if (n) {
					printk("Result buffer corruption %u "
					       "bytes:\n", n);
					hexdump(q, n);
				}

				temp += template[i].tap[k];
			}
		}
	}

out:
	crypto_free_aead(tfm);
	aead_request_free(req);
}

static void test_cipher(char *algo, int enc,
			struct cipher_testvec *template, unsigned int tcount)
{
	unsigned int ret, i, j, k, n, temp;
	char *q;
	struct crypto_ablkcipher *tfm;
	struct ablkcipher_request *req;
	struct scatterlist sg[8];
	const char *e;
	struct tcrypt_result result;
	void *data;
	char iv[MAX_IVLEN];

	if (enc == ENCRYPT)
	        e = "encryption";
	else
		e = "decryption";

	printk("\ntesting %s %s\n", algo, e);

	init_completion(&result.completion);
	tfm = crypto_alloc_ablkcipher(algo, 0, 0);

	if (IS_ERR(tfm)) {
		printk("failed to load transform for %s: %ld\n", algo,
		       PTR_ERR(tfm));
		return;
	}

	req = ablkcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		printk("failed to allocate request for %s\n", algo);
		goto out;
	}

	ablkcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					tcrypt_complete, &result);

	j = 0;
	for (i = 0; i < tcount; i++) {

		data = kzalloc(template[i].ilen, GFP_KERNEL);
		if (!data)
			continue;

		memcpy(data, template[i].input, template[i].ilen);
		if (template[i].iv)
			memcpy(iv, template[i].iv, MAX_IVLEN);
		else
			memset(iv, 0, MAX_IVLEN);

		if (!(template[i].np)) {
			j++;
			printk("test %u (%d bit key):\n",
			j, template[i].klen * 8);

			crypto_ablkcipher_clear_flags(tfm, ~0);
			if (template[i].wk)
				crypto_ablkcipher_set_flags(
					tfm, CRYPTO_TFM_REQ_WEAK_KEY);

			ret = crypto_ablkcipher_setkey(tfm, template[i].key,
						       template[i].klen);
			if (ret) {
				printk("setkey() failed flags=%x\n",
				       crypto_ablkcipher_get_flags(tfm));

				if (!template[i].fail) {
					kfree(data);
					goto out;
				}
			}

			sg_init_one(&sg[0], data, template[i].ilen);

			ablkcipher_request_set_crypt(req, sg, sg,
						     template[i].ilen, iv);
			ret = enc ?
				crypto_ablkcipher_encrypt(req) :
				crypto_ablkcipher_decrypt(req);

			switch (ret) {
			case 0:
				break;
			case -EINPROGRESS:
			case -EBUSY:
				ret = wait_for_completion_interruptible(
					&result.completion);
				if (!ret && !((ret = result.err))) {
					INIT_COMPLETION(result.completion);
					break;
				}
				/* fall through */
			default:
				printk("%s () failed err=%d\n", e, -ret);
				kfree(data);
				goto out;
			}

			q = data;
			hexdump(q, template[i].rlen);

			printk("%s\n",
			       memcmp(q, template[i].result,
				      template[i].rlen) ? "fail" : "pass");
		}
		kfree(data);
	}

	printk("\ntesting %s %s across pages (chunking)\n", algo, e);

	j = 0;
	for (i = 0; i < tcount; i++) {

		if (template[i].iv)
			memcpy(iv, template[i].iv, MAX_IVLEN);
		else
			memset(iv, 0, MAX_IVLEN);

		if (template[i].np) {
			j++;
			printk("test %u (%d bit key):\n",
			j, template[i].klen * 8);

			memset(xbuf, 0, XBUFSIZE);
			crypto_ablkcipher_clear_flags(tfm, ~0);
			if (template[i].wk)
				crypto_ablkcipher_set_flags(
					tfm, CRYPTO_TFM_REQ_WEAK_KEY);

			ret = crypto_ablkcipher_setkey(tfm, template[i].key,
						       template[i].klen);
			if (ret) {
				printk("setkey() failed flags=%x\n",
						crypto_ablkcipher_get_flags(tfm));

				if (!template[i].fail)
					goto out;
			}

			temp = 0;
			sg_init_table(sg, template[i].np);
			for (k = 0; k < template[i].np; k++) {
				memcpy(&xbuf[IDX[k]],
						template[i].input + temp,
						template[i].tap[k]);
				temp += template[i].tap[k];
				sg_set_buf(&sg[k], &xbuf[IDX[k]],
						template[i].tap[k]);
			}

			ablkcipher_request_set_crypt(req, sg, sg,
					template[i].ilen, iv);

			ret = enc ?
				crypto_ablkcipher_encrypt(req) :
				crypto_ablkcipher_decrypt(req);

			switch (ret) {
			case 0:
				break;
			case -EINPROGRESS:
			case -EBUSY:
				ret = wait_for_completion_interruptible(
					&result.completion);
				if (!ret && !((ret = result.err))) {
					INIT_COMPLETION(result.completion);
					break;
				}
				/* fall through */
			default:
				printk("%s () failed err=%d\n", e, -ret);
				goto out;
			}

			temp = 0;
			for (k = 0; k < template[i].np; k++) {
				printk("page %u\n", k);
				q = &xbuf[IDX[k]];
				hexdump(q, template[i].tap[k]);
				printk("%s\n",
					memcmp(q, template[i].result + temp,
						template[i].tap[k]) ? "fail" :
					"pass");

				for (n = 0; q[template[i].tap[k] + n]; n++)
					;
				if (n) {
					printk("Result buffer corruption %u "
					       "bytes:\n", n);
					hexdump(&q[template[i].tap[k]], n);
				}
				temp += template[i].tap[k];
			}
		}
	}
out:
	crypto_free_ablkcipher(tfm);
	ablkcipher_request_free(req);
}

static int test_cipher_jiffies(struct blkcipher_desc *desc, int enc, char *p,
			       int blen, int sec)
{
	struct scatterlist sg[1];
static int test_cipher_jiffies(struct blkcipher_desc *desc, int enc,
			       struct scatterlist *sg, int blen, int secs)
{
	unsigned long start, end;
	int bcount;
	int ret;

	sg_init_one(sg, p, blen);

	for (start = jiffies, end = start + sec * HZ, bcount = 0;
	for (start = jiffies, end = start + secs * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		if (enc)
			ret = crypto_blkcipher_encrypt(desc, sg, sg, blen);
		else
			ret = crypto_blkcipher_decrypt(desc, sg, sg, blen);

		if (ret)
			return ret;
	}

	printk("%d operations in %d seconds (%ld bytes)\n",
	       bcount, sec, (long)bcount * blen);
	return 0;
}

static int test_cipher_cycles(struct blkcipher_desc *desc, int enc, char *p,
			      int blen)
{
	struct scatterlist sg[1];
	       bcount, secs, (long)bcount * blen);
	return 0;
}

static int test_cipher_cycles(struct blkcipher_desc *desc, int enc,
			      struct scatterlist *sg, int blen)
{
	unsigned long cycles = 0;
	int ret = 0;
	int i;

	sg_init_one(sg, p, blen);

	local_bh_disable();
	local_irq_disable();

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		if (enc)
			ret = crypto_blkcipher_encrypt(desc, sg, sg, blen);
		else
			ret = crypto_blkcipher_decrypt(desc, sg, sg, blen);

		if (ret)
			goto out;
	}

	/* The real thing. */
	for (i = 0; i < 8; i++) {
		cycles_t start, end;

		start = get_cycles();
		if (enc)
			ret = crypto_blkcipher_encrypt(desc, sg, sg, blen);
		else
			ret = crypto_blkcipher_decrypt(desc, sg, sg, blen);
		end = get_cycles();

		if (ret)
			goto out;

		cycles += end - start;
	}

out:
	local_irq_enable();
	local_bh_enable();

	if (ret == 0)
		printk("1 operation in %lu cycles (%d bytes)\n",
		       (cycles + 4) / 8, blen);

	return ret;
}

static inline int do_one_aead_op(struct aead_request *req, int ret)
{
	if (ret == -EINPROGRESS || ret == -EBUSY) {
		struct tcrypt_result *tr = req->base.data;

		ret = wait_for_completion_interruptible(&tr->completion);
		if (!ret)
			ret = tr->err;
		reinit_completion(&tr->completion);
	}

	return ret;
}

static int test_aead_jiffies(struct aead_request *req, int enc,
				int blen, int secs)
{
	unsigned long start, end;
	int bcount;
	int ret;

	for (start = jiffies, end = start + secs * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		if (enc)
			ret = do_one_aead_op(req, crypto_aead_encrypt(req));
		else
			ret = do_one_aead_op(req, crypto_aead_decrypt(req));

		if (ret)
			return ret;
	}

	printk("%d operations in %d seconds (%ld bytes)\n",
	       bcount, secs, (long)bcount * blen);
	return 0;
}

static int test_aead_cycles(struct aead_request *req, int enc, int blen)
{
	unsigned long cycles = 0;
	int ret = 0;
	int i;

	local_irq_disable();

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		if (enc)
			ret = do_one_aead_op(req, crypto_aead_encrypt(req));
		else
			ret = do_one_aead_op(req, crypto_aead_decrypt(req));

		if (ret)
			goto out;
	}

	/* The real thing. */
	for (i = 0; i < 8; i++) {
		cycles_t start, end;

		start = get_cycles();
		if (enc)
			ret = do_one_aead_op(req, crypto_aead_encrypt(req));
		else
			ret = do_one_aead_op(req, crypto_aead_decrypt(req));
		end = get_cycles();

		if (ret)
			goto out;

		cycles += end - start;
	}

out:
	local_irq_enable();

	if (ret == 0)
		printk("1 operation in %lu cycles (%d bytes)\n",
		       (cycles + 4) / 8, blen);

	return ret;
}

static u32 block_sizes[] = { 16, 64, 256, 1024, 8192, 0 };

static void test_cipher_speed(char *algo, int enc, unsigned int sec,
			      struct cipher_testvec *template,
			      unsigned int tcount, u8 *keysize)
{
	unsigned int ret, i, j, iv_len;
	unsigned char *key, *p, iv[128];
static u32 aead_sizes[] = { 16, 64, 256, 512, 1024, 2048, 4096, 8192, 0 };

#define XBUFSIZE 8
#define MAX_IVLEN 32

static int testmgr_alloc_buf(char *buf[XBUFSIZE])
{
	int i;

	for (i = 0; i < XBUFSIZE; i++) {
		buf[i] = (void *)__get_free_page(GFP_KERNEL);
		if (!buf[i])
			goto err_free_buf;
	}

	return 0;

err_free_buf:
	while (i-- > 0)
		free_page((unsigned long)buf[i]);

	return -ENOMEM;
}

static void testmgr_free_buf(char *buf[XBUFSIZE])
{
	int i;

	for (i = 0; i < XBUFSIZE; i++)
		free_page((unsigned long)buf[i]);
}

static void sg_init_aead(struct scatterlist *sg, char *xbuf[XBUFSIZE],
			unsigned int buflen)
{
	int np = (buflen + PAGE_SIZE - 1)/PAGE_SIZE;
	int k, rem;

	if (np > XBUFSIZE) {
		rem = PAGE_SIZE;
		np = XBUFSIZE;
	} else {
		rem = buflen % PAGE_SIZE;
	}

	sg_init_table(sg, np + 1);
	if (rem)
		np--;
	for (k = 0; k < np; k++)
		sg_set_buf(&sg[k + 1], xbuf[k], PAGE_SIZE);

	if (rem)
		sg_set_buf(&sg[k + 1], xbuf[k], rem);
}

static void test_aead_speed(const char *algo, int enc, unsigned int secs,
			    struct aead_speed_template *template,
			    unsigned int tcount, u8 authsize,
			    unsigned int aad_size, u8 *keysize)
{
	unsigned int i, j;
	struct crypto_aead *tfm;
	int ret = -ENOMEM;
	const char *key;
	struct aead_request *req;
	struct scatterlist *sg;
	struct scatterlist *sgout;
	const char *e;
	void *assoc;
	char *iv;
	char *xbuf[XBUFSIZE];
	char *xoutbuf[XBUFSIZE];
	char *axbuf[XBUFSIZE];
	unsigned int *b_size;
	unsigned int iv_len;
	struct tcrypt_result result;

	iv = kzalloc(MAX_IVLEN, GFP_KERNEL);
	if (!iv)
		return;

	if (aad_size >= PAGE_SIZE) {
		pr_err("associate data length (%u) too big\n", aad_size);
		goto out_noxbuf;
	}

	if (enc == ENCRYPT)
		e = "encryption";
	else
		e = "decryption";

	if (testmgr_alloc_buf(xbuf))
		goto out_noxbuf;
	if (testmgr_alloc_buf(axbuf))
		goto out_noaxbuf;
	if (testmgr_alloc_buf(xoutbuf))
		goto out_nooutbuf;

	sg = kmalloc(sizeof(*sg) * 9 * 2, GFP_KERNEL);
	if (!sg)
		goto out_nosg;
	sgout = &sg[9];

	tfm = crypto_alloc_aead(algo, 0, 0);

	if (IS_ERR(tfm)) {
		pr_err("alg: aead: Failed to load transform for %s: %ld\n", algo,
		       PTR_ERR(tfm));
		goto out_notfm;
	}

	init_completion(&result.completion);
	printk(KERN_INFO "\ntesting speed of %s (%s) %s\n", algo,
			get_driver_name(crypto_aead, tfm), e);

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("alg: aead: Failed to allocate request for %s\n",
		       algo);
		goto out_noreq;
	}

	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  tcrypt_complete, &result);

	i = 0;
	do {
		b_size = aead_sizes;
		do {
			assoc = axbuf[0];
			memset(assoc, 0xff, aad_size);

			if ((*keysize + *b_size) > TVMEMSIZE * PAGE_SIZE) {
				pr_err("template (%u) too big for tvmem (%lu)\n",
				       *keysize + *b_size,
					TVMEMSIZE * PAGE_SIZE);
				goto out;
			}

			key = tvmem[0];
			for (j = 0; j < tcount; j++) {
				if (template[j].klen == *keysize) {
					key = template[j].key;
					break;
				}
			}
			ret = crypto_aead_setkey(tfm, key, *keysize);
			ret = crypto_aead_setauthsize(tfm, authsize);

			iv_len = crypto_aead_ivsize(tfm);
			if (iv_len)
				memset(iv, 0xff, iv_len);

			crypto_aead_clear_flags(tfm, ~0);
			printk(KERN_INFO "test %u (%d bit key, %d byte blocks): ",
					i, *keysize * 8, *b_size);


			memset(tvmem[0], 0xff, PAGE_SIZE);

			if (ret) {
				pr_err("setkey() failed flags=%x\n",
						crypto_aead_get_flags(tfm));
				goto out;
			}

			sg_init_aead(sg, xbuf,
				    *b_size + (enc ? 0 : authsize));

			sg_init_aead(sgout, xoutbuf,
				    *b_size + (enc ? authsize : 0));

			sg_set_buf(&sg[0], assoc, aad_size);
			sg_set_buf(&sgout[0], assoc, aad_size);

			aead_request_set_crypt(req, sg, sgout,
					       *b_size + (enc ? 0 : authsize),
					       iv);
			aead_request_set_ad(req, aad_size);

			if (secs)
				ret = test_aead_jiffies(req, enc, *b_size,
							secs);
			else
				ret = test_aead_cycles(req, enc, *b_size);

			if (ret) {
				pr_err("%s() failed return code=%d\n", e, ret);
				break;
			}
			b_size++;
			i++;
		} while (*b_size);
		keysize++;
	} while (*keysize);

out:
	aead_request_free(req);
out_noreq:
	crypto_free_aead(tfm);
out_notfm:
	kfree(sg);
out_nosg:
	testmgr_free_buf(xoutbuf);
out_nooutbuf:
	testmgr_free_buf(axbuf);
out_noaxbuf:
	testmgr_free_buf(xbuf);
out_noxbuf:
	kfree(iv);
	return;
}

static void test_cipher_speed(const char *algo, int enc, unsigned int secs,
			      struct cipher_speed_template *template,
			      unsigned int tcount, u8 *keysize)
{
	unsigned int ret, i, j, iv_len;
	const char *key;
	char iv[128];
	struct crypto_blkcipher *tfm;
	struct blkcipher_desc desc;
	const char *e;
	u32 *b_size;

	if (enc == ENCRYPT)
	        e = "encryption";
	else
		e = "decryption";

	printk("\ntesting speed of %s %s\n", algo, e);

	tfm = crypto_alloc_blkcipher(algo, 0, CRYPTO_ALG_ASYNC);

	if (IS_ERR(tfm)) {
		printk("failed to load transform for %s: %ld\n", algo,
		       PTR_ERR(tfm));
		return;
	}
	desc.tfm = tfm;
	desc.flags = 0;

	printk(KERN_INFO "\ntesting speed of %s (%s) %s\n", algo,
			get_driver_name(crypto_blkcipher, tfm), e);

	i = 0;
	do {

		b_size = block_sizes;
		do {

			if ((*keysize + *b_size) > TVMEMSIZE) {
				printk("template (%u) too big for tvmem (%u)\n",
						*keysize + *b_size, TVMEMSIZE);
			struct scatterlist sg[TVMEMSIZE];

			if ((*keysize + *b_size) > TVMEMSIZE * PAGE_SIZE) {
				printk("template (%u) too big for "
				       "tvmem (%lu)\n", *keysize + *b_size,
				       TVMEMSIZE * PAGE_SIZE);
				goto out;
			}

			printk("test %u (%d bit key, %d byte blocks): ", i,
					*keysize * 8, *b_size);

			memset(tvmem, 0xff, *keysize + *b_size);

			/* set key, plain text and IV */
			key = (unsigned char *)tvmem;
			memset(tvmem[0], 0xff, PAGE_SIZE);

			/* set key, plain text and IV */
			key = tvmem[0];
			for (j = 0; j < tcount; j++) {
				if (template[j].klen == *keysize) {
					key = template[j].key;
					break;
				}
			}
			p = (unsigned char *)tvmem + *keysize;

			ret = crypto_blkcipher_setkey(tfm, key, *keysize);
			if (ret) {
				printk("setkey() failed flags=%x\n",
						crypto_blkcipher_get_flags(tfm));
				goto out;
			}

			sg_init_table(sg, TVMEMSIZE);
			sg_set_buf(sg, tvmem[0] + *keysize,
				   PAGE_SIZE - *keysize);
			for (j = 1; j < TVMEMSIZE; j++) {
				sg_set_buf(sg + j, tvmem[j], PAGE_SIZE);
				memset (tvmem[j], 0xff, PAGE_SIZE);
			}

			iv_len = crypto_blkcipher_ivsize(tfm);
			if (iv_len) {
				memset(&iv, 0xff, iv_len);
				crypto_blkcipher_set_iv(tfm, iv, iv_len);
			}

			if (sec)
				ret = test_cipher_jiffies(&desc, enc, p, *b_size, sec);
			else
				ret = test_cipher_cycles(&desc, enc, p, *b_size);
			if (secs)
				ret = test_cipher_jiffies(&desc, enc, sg,
							  *b_size, secs);
			else
				ret = test_cipher_cycles(&desc, enc, sg,
							 *b_size);

			if (ret) {
				printk("%s() failed flags=%x\n", e, desc.flags);
				break;
			}
			b_size++;
			i++;
		} while (*b_size);
		keysize++;
	} while (*keysize);

out:
	crypto_free_blkcipher(tfm);
}

static int test_hash_jiffies_digest(struct hash_desc *desc, char *p, int blen,
				    char *out, int sec)
{
	struct scatterlist sg[1];
static int test_hash_jiffies_digest(struct hash_desc *desc,
				    struct scatterlist *sg, int blen,
				    char *out, int secs)
{
	unsigned long start, end;
	int bcount;
	int ret;

	sg_init_table(sg, 1);

	for (start = jiffies, end = start + sec * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		sg_set_buf(sg, p, blen);
	for (start = jiffies, end = start + secs * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		ret = crypto_hash_digest(desc, sg, blen, out);
		if (ret)
			return ret;
	}

	printk("%6u opers/sec, %9lu bytes/sec\n",
	       bcount / sec, ((long)bcount * blen) / sec);
	       bcount / secs, ((long)bcount * blen) / secs);

	return 0;
}

static int test_hash_jiffies(struct hash_desc *desc, char *p, int blen,
			     int plen, char *out, int sec)
{
	struct scatterlist sg[1];
static int test_hash_jiffies(struct hash_desc *desc, struct scatterlist *sg,
			     int blen, int plen, char *out, int secs)
{
	unsigned long start, end;
	int bcount, pcount;
	int ret;

	if (plen == blen)
		return test_hash_jiffies_digest(desc, p, blen, out, sec);

	sg_init_table(sg, 1);

	for (start = jiffies, end = start + sec * HZ, bcount = 0;
		return test_hash_jiffies_digest(desc, sg, blen, out, secs);

	for (start = jiffies, end = start + secs * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		ret = crypto_hash_init(desc);
		if (ret)
			return ret;
		for (pcount = 0; pcount < blen; pcount += plen) {
			sg_set_buf(sg, p + pcount, plen);
			ret = crypto_hash_update(desc, sg, plen);
			if (ret)
				return ret;
		}
		/* we assume there is enough space in 'out' for the result */
		ret = crypto_hash_final(desc, out);
		if (ret)
			return ret;
	}

	printk("%6u opers/sec, %9lu bytes/sec\n",
	       bcount / sec, ((long)bcount * blen) / sec);
	       bcount / secs, ((long)bcount * blen) / secs);

	return 0;
}

static int test_hash_cycles_digest(struct hash_desc *desc, char *p, int blen,
				   char *out)
{
	struct scatterlist sg[1];
static int test_hash_cycles_digest(struct hash_desc *desc,
				   struct scatterlist *sg, int blen, char *out)
{
	unsigned long cycles = 0;
	int i;
	int ret;

	sg_init_table(sg, 1);

	local_bh_disable();
	local_irq_disable();

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		sg_set_buf(sg, p, blen);
		ret = crypto_hash_digest(desc, sg, blen, out);
		if (ret)
			goto out;
	}

	/* The real thing. */
	for (i = 0; i < 8; i++) {
		cycles_t start, end;

		start = get_cycles();

		sg_set_buf(sg, p, blen);
		ret = crypto_hash_digest(desc, sg, blen, out);
		if (ret)
			goto out;

		end = get_cycles();

		cycles += end - start;
	}

out:
	local_irq_enable();
	local_bh_enable();

	if (ret)
		return ret;

	printk("%6lu cycles/operation, %4lu cycles/byte\n",
	       cycles / 8, cycles / (8 * blen));

	return 0;
}

static int test_hash_cycles(struct hash_desc *desc, char *p, int blen,
			    int plen, char *out)
{
	struct scatterlist sg[1];
static int test_hash_cycles(struct hash_desc *desc, struct scatterlist *sg,
			    int blen, int plen, char *out)
{
	unsigned long cycles = 0;
	int i, pcount;
	int ret;

	if (plen == blen)
		return test_hash_cycles_digest(desc, p, blen, out);

	sg_init_table(sg, 1);

	local_bh_disable();
		return test_hash_cycles_digest(desc, sg, blen, out);

	local_irq_disable();

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		ret = crypto_hash_init(desc);
		if (ret)
			goto out;
		for (pcount = 0; pcount < blen; pcount += plen) {
			sg_set_buf(sg, p + pcount, plen);
			ret = crypto_hash_update(desc, sg, plen);
			if (ret)
				goto out;
		}
		ret = crypto_hash_final(desc, out);
		if (ret)
			goto out;
	}

	/* The real thing. */
	for (i = 0; i < 8; i++) {
		cycles_t start, end;

		start = get_cycles();

		ret = crypto_hash_init(desc);
		if (ret)
			goto out;
		for (pcount = 0; pcount < blen; pcount += plen) {
			sg_set_buf(sg, p + pcount, plen);
			ret = crypto_hash_update(desc, sg, plen);
			if (ret)
				goto out;
		}
		ret = crypto_hash_final(desc, out);
		if (ret)
			goto out;

		end = get_cycles();

		cycles += end - start;
	}

out:
	local_irq_enable();
	local_bh_enable();

	if (ret)
		return ret;

	printk("%6lu cycles/operation, %4lu cycles/byte\n",
	       cycles / 8, cycles / (8 * blen));

	return 0;
}

static void test_hash_speed(char *algo, unsigned int sec,
			      struct hash_speed *speed)
{
	struct crypto_hash *tfm;
	struct hash_desc desc;
	char output[1024];
	int i;
	int ret;

	printk("\ntesting speed of %s\n", algo);

	tfm = crypto_alloc_hash(algo, 0, CRYPTO_ALG_ASYNC);

	if (IS_ERR(tfm)) {
		printk("failed to load transform for %s: %ld\n", algo,
static void test_hash_sg_init(struct scatterlist *sg)
{
	int i;

	sg_init_table(sg, TVMEMSIZE);
	for (i = 0; i < TVMEMSIZE; i++) {
		sg_set_buf(sg + i, tvmem[i], PAGE_SIZE);
		memset(tvmem[i], 0xff, PAGE_SIZE);
	}
}

static void test_hash_speed(const char *algo, unsigned int secs,
			    struct hash_speed *speed)
{
	struct scatterlist sg[TVMEMSIZE];
	struct crypto_hash *tfm;
	struct hash_desc desc;
	static char output[1024];
	int i;
	int ret;

	tfm = crypto_alloc_hash(algo, 0, CRYPTO_ALG_ASYNC);

	if (IS_ERR(tfm)) {
		printk(KERN_ERR "failed to load transform for %s: %ld\n", algo,
		       PTR_ERR(tfm));
		return;
	}

	printk(KERN_INFO "\ntesting speed of %s (%s)\n", algo,
			get_driver_name(crypto_hash, tfm));

	desc.tfm = tfm;
	desc.flags = 0;

	if (crypto_hash_digestsize(tfm) > sizeof(output)) {
		printk("digestsize(%u) > outputbuffer(%zu)\n",
		printk(KERN_ERR "digestsize(%u) > outputbuffer(%zu)\n",
		       crypto_hash_digestsize(tfm), sizeof(output));
		goto out;
	}

	for (i = 0; speed[i].blen != 0; i++) {
		if (speed[i].blen > TVMEMSIZE) {
			printk("template (%u) too big for tvmem (%u)\n",
			       speed[i].blen, TVMEMSIZE);
			goto out;
		}

		printk("test%3u (%5u byte blocks,%5u bytes per update,%4u updates): ",
		       i, speed[i].blen, speed[i].plen, speed[i].blen / speed[i].plen);

		memset(tvmem, 0xff, speed[i].blen);

		if (sec)
			ret = test_hash_jiffies(&desc, tvmem, speed[i].blen,
						speed[i].plen, output, sec);
		else
			ret = test_hash_cycles(&desc, tvmem, speed[i].blen,
					       speed[i].plen, output);

		if (ret) {
			printk("hashing failed ret=%d\n", ret);
	test_hash_sg_init(sg);
	for (i = 0; speed[i].blen != 0; i++) {
		if (speed[i].blen > TVMEMSIZE * PAGE_SIZE) {
			printk(KERN_ERR
			       "template (%u) too big for tvmem (%lu)\n",
			       speed[i].blen, TVMEMSIZE * PAGE_SIZE);
			goto out;
		}

		if (speed[i].klen)
			crypto_hash_setkey(tfm, tvmem[0], speed[i].klen);

		printk(KERN_INFO "test%3u "
		       "(%5u byte blocks,%5u bytes per update,%4u updates): ",
		       i, speed[i].blen, speed[i].plen, speed[i].blen / speed[i].plen);

		if (secs)
			ret = test_hash_jiffies(&desc, sg, speed[i].blen,
						speed[i].plen, output, secs);
		else
			ret = test_hash_cycles(&desc, sg, speed[i].blen,
					       speed[i].plen, output);

		if (ret) {
			printk(KERN_ERR "hashing failed ret=%d\n", ret);
			break;
		}
	}

out:
	crypto_free_hash(tfm);
}

static void test_comp(char *algo, struct comp_testvec *ctemplate,
		       struct comp_testvec *dtemplate, int ctcount, int dtcount)
{
	unsigned int i;
	char result[COMP_BUF_SIZE];
	struct crypto_comp *tfm;
	unsigned int tsize;

	printk("\ntesting %s compression\n", algo);

	tfm = crypto_alloc_comp(algo, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		printk("failed to load transform for %s\n", algo);
		return;
	}

	for (i = 0; i < ctcount; i++) {
		int ilen, ret, dlen = COMP_BUF_SIZE;

		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		ilen = ctemplate[i].inlen;
		ret = crypto_comp_compress(tfm, ctemplate[i].input,
		                           ilen, result, &dlen);
		if (ret) {
			printk("fail: ret=%d\n", ret);
			continue;
		}
		hexdump(result, dlen);
		printk("%s (ratio %d:%d)\n",
		       memcmp(result, ctemplate[i].output, dlen) ? "fail" : "pass",
		       ilen, dlen);
	}

	printk("\ntesting %s decompression\n", algo);

	tsize = sizeof(struct comp_testvec);
	tsize *= dtcount;
	if (tsize > TVMEMSIZE) {
		printk("template (%u) too big for tvmem (%u)\n", tsize,
		       TVMEMSIZE);
		goto out;
	}

	for (i = 0; i < dtcount; i++) {
		int ilen, ret, dlen = COMP_BUF_SIZE;

		printk("test %u:\n", i + 1);
		memset(result, 0, sizeof (result));

		ilen = dtemplate[i].inlen;
		ret = crypto_comp_decompress(tfm, dtemplate[i].input,
		                             ilen, result, &dlen);
		if (ret) {
			printk("fail: ret=%d\n", ret);
			continue;
		}
		hexdump(result, dlen);
		printk("%s (ratio %d:%d)\n",
		       memcmp(result, dtemplate[i].output, dlen) ? "fail" : "pass",
		       ilen, dlen);
	}
out:
	crypto_free_comp(tfm);
static inline int do_one_ahash_op(struct ahash_request *req, int ret)
{
	if (ret == -EINPROGRESS || ret == -EBUSY) {
		struct tcrypt_result *tr = req->base.data;

		wait_for_completion(&tr->completion);
		reinit_completion(&tr->completion);
		ret = tr->err;
	}
	return ret;
}

static int test_ahash_jiffies_digest(struct ahash_request *req, int blen,
				     char *out, int secs)
{
	unsigned long start, end;
	int bcount;
	int ret;

	for (start = jiffies, end = start + secs * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		ret = do_one_ahash_op(req, crypto_ahash_digest(req));
		if (ret)
			return ret;
	}

	printk("%6u opers/sec, %9lu bytes/sec\n",
	       bcount / secs, ((long)bcount * blen) / secs);

	return 0;
}

static int test_ahash_jiffies(struct ahash_request *req, int blen,
			      int plen, char *out, int secs)
{
	unsigned long start, end;
	int bcount, pcount;
	int ret;

	if (plen == blen)
		return test_ahash_jiffies_digest(req, blen, out, secs);

	for (start = jiffies, end = start + secs * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		ret = do_one_ahash_op(req, crypto_ahash_init(req));
		if (ret)
			return ret;
		for (pcount = 0; pcount < blen; pcount += plen) {
			ret = do_one_ahash_op(req, crypto_ahash_update(req));
			if (ret)
				return ret;
		}
		/* we assume there is enough space in 'out' for the result */
		ret = do_one_ahash_op(req, crypto_ahash_final(req));
		if (ret)
			return ret;
	}

	pr_cont("%6u opers/sec, %9lu bytes/sec\n",
		bcount / secs, ((long)bcount * blen) / secs);

	return 0;
}

static int test_ahash_cycles_digest(struct ahash_request *req, int blen,
				    char *out)
{
	unsigned long cycles = 0;
	int ret, i;

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		ret = do_one_ahash_op(req, crypto_ahash_digest(req));
		if (ret)
			goto out;
	}

	/* The real thing. */
	for (i = 0; i < 8; i++) {
		cycles_t start, end;

		start = get_cycles();

		ret = do_one_ahash_op(req, crypto_ahash_digest(req));
		if (ret)
			goto out;

		end = get_cycles();

		cycles += end - start;
	}

out:
	if (ret)
		return ret;

	pr_cont("%6lu cycles/operation, %4lu cycles/byte\n",
		cycles / 8, cycles / (8 * blen));

	return 0;
}

static int test_ahash_cycles(struct ahash_request *req, int blen,
			     int plen, char *out)
{
	unsigned long cycles = 0;
	int i, pcount, ret;

	if (plen == blen)
		return test_ahash_cycles_digest(req, blen, out);

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		ret = do_one_ahash_op(req, crypto_ahash_init(req));
		if (ret)
			goto out;
		for (pcount = 0; pcount < blen; pcount += plen) {
			ret = do_one_ahash_op(req, crypto_ahash_update(req));
			if (ret)
				goto out;
		}
		ret = do_one_ahash_op(req, crypto_ahash_final(req));
		if (ret)
			goto out;
	}

	/* The real thing. */
	for (i = 0; i < 8; i++) {
		cycles_t start, end;

		start = get_cycles();

		ret = do_one_ahash_op(req, crypto_ahash_init(req));
		if (ret)
			goto out;
		for (pcount = 0; pcount < blen; pcount += plen) {
			ret = do_one_ahash_op(req, crypto_ahash_update(req));
			if (ret)
				goto out;
		}
		ret = do_one_ahash_op(req, crypto_ahash_final(req));
		if (ret)
			goto out;

		end = get_cycles();

		cycles += end - start;
	}

out:
	if (ret)
		return ret;

	pr_cont("%6lu cycles/operation, %4lu cycles/byte\n",
		cycles / 8, cycles / (8 * blen));

	return 0;
}

static void test_ahash_speed(const char *algo, unsigned int secs,
			     struct hash_speed *speed)
{
	struct scatterlist sg[TVMEMSIZE];
	struct tcrypt_result tresult;
	struct ahash_request *req;
	struct crypto_ahash *tfm;
	char *output;
	int i, ret;

	tfm = crypto_alloc_ahash(algo, 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("failed to load transform for %s: %ld\n",
		       algo, PTR_ERR(tfm));
		return;
	}

	printk(KERN_INFO "\ntesting speed of async %s (%s)\n", algo,
			get_driver_name(crypto_ahash, tfm));

	if (crypto_ahash_digestsize(tfm) > MAX_DIGEST_SIZE) {
		pr_err("digestsize(%u) > %d\n", crypto_ahash_digestsize(tfm),
		       MAX_DIGEST_SIZE);
		goto out;
	}

	test_hash_sg_init(sg);
	req = ahash_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("ahash request allocation failure\n");
		goto out;
	}

	init_completion(&tresult.completion);
	ahash_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   tcrypt_complete, &tresult);

	output = kmalloc(MAX_DIGEST_SIZE, GFP_KERNEL);
	if (!output)
		goto out_nomem;

	for (i = 0; speed[i].blen != 0; i++) {
		if (speed[i].blen > TVMEMSIZE * PAGE_SIZE) {
			pr_err("template (%u) too big for tvmem (%lu)\n",
			       speed[i].blen, TVMEMSIZE * PAGE_SIZE);
			break;
		}

		pr_info("test%3u "
			"(%5u byte blocks,%5u bytes per update,%4u updates): ",
			i, speed[i].blen, speed[i].plen, speed[i].blen / speed[i].plen);

		ahash_request_set_crypt(req, sg, output, speed[i].plen);

		if (secs)
			ret = test_ahash_jiffies(req, speed[i].blen,
						 speed[i].plen, output, secs);
		else
			ret = test_ahash_cycles(req, speed[i].blen,
						speed[i].plen, output);

		if (ret) {
			pr_err("hashing failed ret=%d\n", ret);
			break;
		}
	}

	kfree(output);

out_nomem:
	ahash_request_free(req);

out:
	crypto_free_ahash(tfm);
}

static inline int do_one_acipher_op(struct ablkcipher_request *req, int ret)
{
	if (ret == -EINPROGRESS || ret == -EBUSY) {
		struct tcrypt_result *tr = req->base.data;

		wait_for_completion(&tr->completion);
		reinit_completion(&tr->completion);
		ret = tr->err;
	}

	return ret;
}

static int test_acipher_jiffies(struct ablkcipher_request *req, int enc,
				int blen, int secs)
{
	unsigned long start, end;
	int bcount;
	int ret;

	for (start = jiffies, end = start + secs * HZ, bcount = 0;
	     time_before(jiffies, end); bcount++) {
		if (enc)
			ret = do_one_acipher_op(req,
						crypto_ablkcipher_encrypt(req));
		else
			ret = do_one_acipher_op(req,
						crypto_ablkcipher_decrypt(req));

		if (ret)
			return ret;
	}

	pr_cont("%d operations in %d seconds (%ld bytes)\n",
		bcount, secs, (long)bcount * blen);
	return 0;
}

static int test_acipher_cycles(struct ablkcipher_request *req, int enc,
			       int blen)
{
	unsigned long cycles = 0;
	int ret = 0;
	int i;

	/* Warm-up run. */
	for (i = 0; i < 4; i++) {
		if (enc)
			ret = do_one_acipher_op(req,
						crypto_ablkcipher_encrypt(req));
		else
			ret = do_one_acipher_op(req,
						crypto_ablkcipher_decrypt(req));

		if (ret)
			goto out;
	}

	/* The real thing. */
	for (i = 0; i < 8; i++) {
		cycles_t start, end;

		start = get_cycles();
		if (enc)
			ret = do_one_acipher_op(req,
						crypto_ablkcipher_encrypt(req));
		else
			ret = do_one_acipher_op(req,
						crypto_ablkcipher_decrypt(req));
		end = get_cycles();

		if (ret)
			goto out;

		cycles += end - start;
	}

out:
	if (ret == 0)
		pr_cont("1 operation in %lu cycles (%d bytes)\n",
			(cycles + 4) / 8, blen);

	return ret;
}

static void test_acipher_speed(const char *algo, int enc, unsigned int secs,
			       struct cipher_speed_template *template,
			       unsigned int tcount, u8 *keysize)
{
	unsigned int ret, i, j, k, iv_len;
	struct tcrypt_result tresult;
	const char *key;
	char iv[128];
	struct ablkcipher_request *req;
	struct crypto_ablkcipher *tfm;
	const char *e;
	u32 *b_size;

	if (enc == ENCRYPT)
		e = "encryption";
	else
		e = "decryption";

	init_completion(&tresult.completion);

	tfm = crypto_alloc_ablkcipher(algo, 0, 0);

	if (IS_ERR(tfm)) {
		pr_err("failed to load transform for %s: %ld\n", algo,
		       PTR_ERR(tfm));
		return;
	}

	pr_info("\ntesting speed of async %s (%s) %s\n", algo,
			get_driver_name(crypto_ablkcipher, tfm), e);

	req = ablkcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("tcrypt: skcipher: Failed to allocate request for %s\n",
		       algo);
		goto out;
	}

	ablkcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					tcrypt_complete, &tresult);

	i = 0;
	do {
		b_size = block_sizes;

		do {
			struct scatterlist sg[TVMEMSIZE];

			if ((*keysize + *b_size) > TVMEMSIZE * PAGE_SIZE) {
				pr_err("template (%u) too big for "
				       "tvmem (%lu)\n", *keysize + *b_size,
				       TVMEMSIZE * PAGE_SIZE);
				goto out_free_req;
			}

			pr_info("test %u (%d bit key, %d byte blocks): ", i,
				*keysize * 8, *b_size);

			memset(tvmem[0], 0xff, PAGE_SIZE);

			/* set key, plain text and IV */
			key = tvmem[0];
			for (j = 0; j < tcount; j++) {
				if (template[j].klen == *keysize) {
					key = template[j].key;
					break;
				}
			}

			crypto_ablkcipher_clear_flags(tfm, ~0);

			ret = crypto_ablkcipher_setkey(tfm, key, *keysize);
			if (ret) {
				pr_err("setkey() failed flags=%x\n",
					crypto_ablkcipher_get_flags(tfm));
				goto out_free_req;
			}

			k = *keysize + *b_size;
			sg_init_table(sg, DIV_ROUND_UP(k, PAGE_SIZE));

			if (k > PAGE_SIZE) {
				sg_set_buf(sg, tvmem[0] + *keysize,
				   PAGE_SIZE - *keysize);
				k -= PAGE_SIZE;
				j = 1;
				while (k > PAGE_SIZE) {
					sg_set_buf(sg + j, tvmem[j], PAGE_SIZE);
					memset(tvmem[j], 0xff, PAGE_SIZE);
					j++;
					k -= PAGE_SIZE;
				}
				sg_set_buf(sg + j, tvmem[j], k);
				memset(tvmem[j], 0xff, k);
			} else {
				sg_set_buf(sg, tvmem[0] + *keysize, *b_size);
			}

			iv_len = crypto_ablkcipher_ivsize(tfm);
			if (iv_len)
				memset(&iv, 0xff, iv_len);

			ablkcipher_request_set_crypt(req, sg, sg, *b_size, iv);

			if (secs)
				ret = test_acipher_jiffies(req, enc,
							   *b_size, secs);
			else
				ret = test_acipher_cycles(req, enc,
							  *b_size);

			if (ret) {
				pr_err("%s() failed flags=%x\n", e,
					crypto_ablkcipher_get_flags(tfm));
				break;
			}
			b_size++;
			i++;
		} while (*b_size);
		keysize++;
	} while (*keysize);

out_free_req:
	ablkcipher_request_free(req);
out:
	crypto_free_ablkcipher(tfm);
}

static void test_available(void)
{
	char **name = check;

	while (*name) {
		printk("alg %s ", *name);
		printk(crypto_has_alg(*name, 0, 0) ?
		       "found\n" : "not found\n");
		name++;
	}
}

static void do_test(void)
{
	switch (mode) {

	case 0:
		test_hash("md5", md5_tv_template, MD5_TEST_VECTORS);

		test_hash("sha1", sha1_tv_template, SHA1_TEST_VECTORS);

		//DES
		test_cipher("ecb(des)", ENCRYPT, des_enc_tv_template,
			    DES_ENC_TEST_VECTORS);
		test_cipher("ecb(des)", DECRYPT, des_dec_tv_template,
			    DES_DEC_TEST_VECTORS);
		test_cipher("cbc(des)", ENCRYPT, des_cbc_enc_tv_template,
			    DES_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(des)", DECRYPT, des_cbc_dec_tv_template,
			    DES_CBC_DEC_TEST_VECTORS);

		//DES3_EDE
		test_cipher("ecb(des3_ede)", ENCRYPT, des3_ede_enc_tv_template,
			    DES3_EDE_ENC_TEST_VECTORS);
		test_cipher("ecb(des3_ede)", DECRYPT, des3_ede_dec_tv_template,
			    DES3_EDE_DEC_TEST_VECTORS);

		test_cipher("cbc(des3_ede)", ENCRYPT,
			    des3_ede_cbc_enc_tv_template,
			    DES3_EDE_CBC_ENC_TEST_VECTORS);

		test_cipher("cbc(des3_ede)", DECRYPT,
			    des3_ede_cbc_dec_tv_template,
			    DES3_EDE_CBC_DEC_TEST_VECTORS);

		test_hash("md4", md4_tv_template, MD4_TEST_VECTORS);

		test_hash("sha224", sha224_tv_template, SHA224_TEST_VECTORS);

		test_hash("sha256", sha256_tv_template, SHA256_TEST_VECTORS);

		//BLOWFISH
		test_cipher("ecb(blowfish)", ENCRYPT, bf_enc_tv_template,
			    BF_ENC_TEST_VECTORS);
		test_cipher("ecb(blowfish)", DECRYPT, bf_dec_tv_template,
			    BF_DEC_TEST_VECTORS);
		test_cipher("cbc(blowfish)", ENCRYPT, bf_cbc_enc_tv_template,
			    BF_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(blowfish)", DECRYPT, bf_cbc_dec_tv_template,
			    BF_CBC_DEC_TEST_VECTORS);

		//TWOFISH
		test_cipher("ecb(twofish)", ENCRYPT, tf_enc_tv_template,
			    TF_ENC_TEST_VECTORS);
		test_cipher("ecb(twofish)", DECRYPT, tf_dec_tv_template,
			    TF_DEC_TEST_VECTORS);
		test_cipher("cbc(twofish)", ENCRYPT, tf_cbc_enc_tv_template,
			    TF_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(twofish)", DECRYPT, tf_cbc_dec_tv_template,
			    TF_CBC_DEC_TEST_VECTORS);

		//SERPENT
		test_cipher("ecb(serpent)", ENCRYPT, serpent_enc_tv_template,
			    SERPENT_ENC_TEST_VECTORS);
		test_cipher("ecb(serpent)", DECRYPT, serpent_dec_tv_template,
			    SERPENT_DEC_TEST_VECTORS);

		//TNEPRES
		test_cipher("ecb(tnepres)", ENCRYPT, tnepres_enc_tv_template,
			    TNEPRES_ENC_TEST_VECTORS);
		test_cipher("ecb(tnepres)", DECRYPT, tnepres_dec_tv_template,
			    TNEPRES_DEC_TEST_VECTORS);

		//AES
		test_cipher("ecb(aes)", ENCRYPT, aes_enc_tv_template,
			    AES_ENC_TEST_VECTORS);
		test_cipher("ecb(aes)", DECRYPT, aes_dec_tv_template,
			    AES_DEC_TEST_VECTORS);
		test_cipher("cbc(aes)", ENCRYPT, aes_cbc_enc_tv_template,
			    AES_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(aes)", DECRYPT, aes_cbc_dec_tv_template,
			    AES_CBC_DEC_TEST_VECTORS);
		test_cipher("lrw(aes)", ENCRYPT, aes_lrw_enc_tv_template,
			    AES_LRW_ENC_TEST_VECTORS);
		test_cipher("lrw(aes)", DECRYPT, aes_lrw_dec_tv_template,
			    AES_LRW_DEC_TEST_VECTORS);
		test_cipher("xts(aes)", ENCRYPT, aes_xts_enc_tv_template,
			    AES_XTS_ENC_TEST_VECTORS);
		test_cipher("xts(aes)", DECRYPT, aes_xts_dec_tv_template,
			    AES_XTS_DEC_TEST_VECTORS);
		test_cipher("rfc3686(ctr(aes))", ENCRYPT, aes_ctr_enc_tv_template,
			    AES_CTR_ENC_TEST_VECTORS);
		test_cipher("rfc3686(ctr(aes))", DECRYPT, aes_ctr_dec_tv_template,
			    AES_CTR_DEC_TEST_VECTORS);
		test_aead("gcm(aes)", ENCRYPT, aes_gcm_enc_tv_template,
			  AES_GCM_ENC_TEST_VECTORS);
		test_aead("gcm(aes)", DECRYPT, aes_gcm_dec_tv_template,
			  AES_GCM_DEC_TEST_VECTORS);
		test_aead("ccm(aes)", ENCRYPT, aes_ccm_enc_tv_template,
			  AES_CCM_ENC_TEST_VECTORS);
		test_aead("ccm(aes)", DECRYPT, aes_ccm_dec_tv_template,
			  AES_CCM_DEC_TEST_VECTORS);

		//CAST5
		test_cipher("ecb(cast5)", ENCRYPT, cast5_enc_tv_template,
			    CAST5_ENC_TEST_VECTORS);
		test_cipher("ecb(cast5)", DECRYPT, cast5_dec_tv_template,
			    CAST5_DEC_TEST_VECTORS);

		//CAST6
		test_cipher("ecb(cast6)", ENCRYPT, cast6_enc_tv_template,
			    CAST6_ENC_TEST_VECTORS);
		test_cipher("ecb(cast6)", DECRYPT, cast6_dec_tv_template,
			    CAST6_DEC_TEST_VECTORS);

		//ARC4
		test_cipher("ecb(arc4)", ENCRYPT, arc4_enc_tv_template,
			    ARC4_ENC_TEST_VECTORS);
		test_cipher("ecb(arc4)", DECRYPT, arc4_dec_tv_template,
			    ARC4_DEC_TEST_VECTORS);

		//TEA
		test_cipher("ecb(tea)", ENCRYPT, tea_enc_tv_template,
			    TEA_ENC_TEST_VECTORS);
		test_cipher("ecb(tea)", DECRYPT, tea_dec_tv_template,
			    TEA_DEC_TEST_VECTORS);


		//XTEA
		test_cipher("ecb(xtea)", ENCRYPT, xtea_enc_tv_template,
			    XTEA_ENC_TEST_VECTORS);
		test_cipher("ecb(xtea)", DECRYPT, xtea_dec_tv_template,
			    XTEA_DEC_TEST_VECTORS);

		//KHAZAD
		test_cipher("ecb(khazad)", ENCRYPT, khazad_enc_tv_template,
			    KHAZAD_ENC_TEST_VECTORS);
		test_cipher("ecb(khazad)", DECRYPT, khazad_dec_tv_template,
			    KHAZAD_DEC_TEST_VECTORS);

		//ANUBIS
		test_cipher("ecb(anubis)", ENCRYPT, anubis_enc_tv_template,
			    ANUBIS_ENC_TEST_VECTORS);
		test_cipher("ecb(anubis)", DECRYPT, anubis_dec_tv_template,
			    ANUBIS_DEC_TEST_VECTORS);
		test_cipher("cbc(anubis)", ENCRYPT, anubis_cbc_enc_tv_template,
			    ANUBIS_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(anubis)", DECRYPT, anubis_cbc_dec_tv_template,
			    ANUBIS_CBC_ENC_TEST_VECTORS);

		//XETA
		test_cipher("ecb(xeta)", ENCRYPT, xeta_enc_tv_template,
			    XETA_ENC_TEST_VECTORS);
		test_cipher("ecb(xeta)", DECRYPT, xeta_dec_tv_template,
			    XETA_DEC_TEST_VECTORS);

		//FCrypt
		test_cipher("pcbc(fcrypt)", ENCRYPT, fcrypt_pcbc_enc_tv_template,
			    FCRYPT_ENC_TEST_VECTORS);
		test_cipher("pcbc(fcrypt)", DECRYPT, fcrypt_pcbc_dec_tv_template,
			    FCRYPT_DEC_TEST_VECTORS);

		//CAMELLIA
		test_cipher("ecb(camellia)", ENCRYPT,
			    camellia_enc_tv_template,
			    CAMELLIA_ENC_TEST_VECTORS);
		test_cipher("ecb(camellia)", DECRYPT,
			    camellia_dec_tv_template,
			    CAMELLIA_DEC_TEST_VECTORS);
		test_cipher("cbc(camellia)", ENCRYPT,
			    camellia_cbc_enc_tv_template,
			    CAMELLIA_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(camellia)", DECRYPT,
			    camellia_cbc_dec_tv_template,
			    CAMELLIA_CBC_DEC_TEST_VECTORS);

		//SEED
		test_cipher("ecb(seed)", ENCRYPT, seed_enc_tv_template,
			    SEED_ENC_TEST_VECTORS);
		test_cipher("ecb(seed)", DECRYPT, seed_dec_tv_template,
			    SEED_DEC_TEST_VECTORS);

		//CTS
		test_cipher("cts(cbc(aes))", ENCRYPT, cts_mode_enc_tv_template,
			    CTS_MODE_ENC_TEST_VECTORS);
		test_cipher("cts(cbc(aes))", DECRYPT, cts_mode_dec_tv_template,
			    CTS_MODE_DEC_TEST_VECTORS);

		test_hash("sha384", sha384_tv_template, SHA384_TEST_VECTORS);
		test_hash("sha512", sha512_tv_template, SHA512_TEST_VECTORS);
		test_hash("wp512", wp512_tv_template, WP512_TEST_VECTORS);
		test_hash("wp384", wp384_tv_template, WP384_TEST_VECTORS);
		test_hash("wp256", wp256_tv_template, WP256_TEST_VECTORS);
		test_hash("tgr192", tgr192_tv_template, TGR192_TEST_VECTORS);
		test_hash("tgr160", tgr160_tv_template, TGR160_TEST_VECTORS);
		test_hash("tgr128", tgr128_tv_template, TGR128_TEST_VECTORS);
		test_comp("deflate", deflate_comp_tv_template,
			  deflate_decomp_tv_template, DEFLATE_COMP_TEST_VECTORS,
			  DEFLATE_DECOMP_TEST_VECTORS);
		test_comp("lzo", lzo_comp_tv_template, lzo_decomp_tv_template,
			  LZO_COMP_TEST_VECTORS, LZO_DECOMP_TEST_VECTORS);
		test_hash("crc32c", crc32c_tv_template, CRC32C_TEST_VECTORS);
		test_hash("hmac(md5)", hmac_md5_tv_template,
			  HMAC_MD5_TEST_VECTORS);
		test_hash("hmac(sha1)", hmac_sha1_tv_template,
			  HMAC_SHA1_TEST_VECTORS);
		test_hash("hmac(sha224)", hmac_sha224_tv_template,
			  HMAC_SHA224_TEST_VECTORS);
		test_hash("hmac(sha256)", hmac_sha256_tv_template,
			  HMAC_SHA256_TEST_VECTORS);
		test_hash("hmac(sha384)", hmac_sha384_tv_template,
			  HMAC_SHA384_TEST_VECTORS);
		test_hash("hmac(sha512)", hmac_sha512_tv_template,
			  HMAC_SHA512_TEST_VECTORS);

		test_hash("xcbc(aes)", aes_xcbc128_tv_template,
			  XCBC_AES_TEST_VECTORS);

		test_hash("michael_mic", michael_mic_tv_template, MICHAEL_MIC_TEST_VECTORS);
		break;

	case 1:
		test_hash("md5", md5_tv_template, MD5_TEST_VECTORS);
		break;

	case 2:
		test_hash("sha1", sha1_tv_template, SHA1_TEST_VECTORS);
		break;

	case 3:
		test_cipher("ecb(des)", ENCRYPT, des_enc_tv_template,
			    DES_ENC_TEST_VECTORS);
		test_cipher("ecb(des)", DECRYPT, des_dec_tv_template,
			    DES_DEC_TEST_VECTORS);
		test_cipher("cbc(des)", ENCRYPT, des_cbc_enc_tv_template,
			    DES_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(des)", DECRYPT, des_cbc_dec_tv_template,
			    DES_CBC_DEC_TEST_VECTORS);
		break;

	case 4:
		test_cipher("ecb(des3_ede)", ENCRYPT, des3_ede_enc_tv_template,
			    DES3_EDE_ENC_TEST_VECTORS);
		test_cipher("ecb(des3_ede)", DECRYPT, des3_ede_dec_tv_template,
			    DES3_EDE_DEC_TEST_VECTORS);

		test_cipher("cbc(des3_ede)", ENCRYPT,
			    des3_ede_cbc_enc_tv_template,
			    DES3_EDE_CBC_ENC_TEST_VECTORS);

		test_cipher("cbc(des3_ede)", DECRYPT,
			    des3_ede_cbc_dec_tv_template,
			    DES3_EDE_CBC_DEC_TEST_VECTORS);
		break;

	case 5:
		test_hash("md4", md4_tv_template, MD4_TEST_VECTORS);
		break;

	case 6:
		test_hash("sha256", sha256_tv_template, SHA256_TEST_VECTORS);
		break;

	case 7:
		test_cipher("ecb(blowfish)", ENCRYPT, bf_enc_tv_template,
			    BF_ENC_TEST_VECTORS);
		test_cipher("ecb(blowfish)", DECRYPT, bf_dec_tv_template,
			    BF_DEC_TEST_VECTORS);
		test_cipher("cbc(blowfish)", ENCRYPT, bf_cbc_enc_tv_template,
			    BF_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(blowfish)", DECRYPT, bf_cbc_dec_tv_template,
			    BF_CBC_DEC_TEST_VECTORS);
		break;

	case 8:
		test_cipher("ecb(twofish)", ENCRYPT, tf_enc_tv_template,
			    TF_ENC_TEST_VECTORS);
		test_cipher("ecb(twofish)", DECRYPT, tf_dec_tv_template,
			    TF_DEC_TEST_VECTORS);
		test_cipher("cbc(twofish)", ENCRYPT, tf_cbc_enc_tv_template,
			    TF_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(twofish)", DECRYPT, tf_cbc_dec_tv_template,
			    TF_CBC_DEC_TEST_VECTORS);
		break;

	case 9:
		test_cipher("ecb(serpent)", ENCRYPT, serpent_enc_tv_template,
			    SERPENT_ENC_TEST_VECTORS);
		test_cipher("ecb(serpent)", DECRYPT, serpent_dec_tv_template,
			    SERPENT_DEC_TEST_VECTORS);
		break;

	case 10:
		test_cipher("ecb(aes)", ENCRYPT, aes_enc_tv_template,
			    AES_ENC_TEST_VECTORS);
		test_cipher("ecb(aes)", DECRYPT, aes_dec_tv_template,
			    AES_DEC_TEST_VECTORS);
		test_cipher("cbc(aes)", ENCRYPT, aes_cbc_enc_tv_template,
			    AES_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(aes)", DECRYPT, aes_cbc_dec_tv_template,
			    AES_CBC_DEC_TEST_VECTORS);
		test_cipher("lrw(aes)", ENCRYPT, aes_lrw_enc_tv_template,
			    AES_LRW_ENC_TEST_VECTORS);
		test_cipher("lrw(aes)", DECRYPT, aes_lrw_dec_tv_template,
			    AES_LRW_DEC_TEST_VECTORS);
		test_cipher("xts(aes)", ENCRYPT, aes_xts_enc_tv_template,
			    AES_XTS_ENC_TEST_VECTORS);
		test_cipher("xts(aes)", DECRYPT, aes_xts_dec_tv_template,
			    AES_XTS_DEC_TEST_VECTORS);
		test_cipher("rfc3686(ctr(aes))", ENCRYPT, aes_ctr_enc_tv_template,
			    AES_CTR_ENC_TEST_VECTORS);
		test_cipher("rfc3686(ctr(aes))", DECRYPT, aes_ctr_dec_tv_template,
			    AES_CTR_DEC_TEST_VECTORS);
		break;

	case 11:
		test_hash("sha384", sha384_tv_template, SHA384_TEST_VECTORS);
		break;

	case 12:
		test_hash("sha512", sha512_tv_template, SHA512_TEST_VECTORS);
		break;

	case 13:
		test_comp("deflate", deflate_comp_tv_template,
			  deflate_decomp_tv_template, DEFLATE_COMP_TEST_VECTORS,
			  DEFLATE_DECOMP_TEST_VECTORS);
		break;

	case 14:
		test_cipher("ecb(cast5)", ENCRYPT, cast5_enc_tv_template,
			    CAST5_ENC_TEST_VECTORS);
		test_cipher("ecb(cast5)", DECRYPT, cast5_dec_tv_template,
			    CAST5_DEC_TEST_VECTORS);
		break;

	case 15:
		test_cipher("ecb(cast6)", ENCRYPT, cast6_enc_tv_template,
			    CAST6_ENC_TEST_VECTORS);
		test_cipher("ecb(cast6)", DECRYPT, cast6_dec_tv_template,
			    CAST6_DEC_TEST_VECTORS);
		break;

	case 16:
		test_cipher("ecb(arc4)", ENCRYPT, arc4_enc_tv_template,
			    ARC4_ENC_TEST_VECTORS);
		test_cipher("ecb(arc4)", DECRYPT, arc4_dec_tv_template,
			    ARC4_DEC_TEST_VECTORS);
		break;

	case 17:
		test_hash("michael_mic", michael_mic_tv_template, MICHAEL_MIC_TEST_VECTORS);
		break;

	case 18:
		test_hash("crc32c", crc32c_tv_template, CRC32C_TEST_VECTORS);
		break;

	case 19:
		test_cipher("ecb(tea)", ENCRYPT, tea_enc_tv_template,
			    TEA_ENC_TEST_VECTORS);
		test_cipher("ecb(tea)", DECRYPT, tea_dec_tv_template,
			    TEA_DEC_TEST_VECTORS);
		break;

	case 20:
		test_cipher("ecb(xtea)", ENCRYPT, xtea_enc_tv_template,
			    XTEA_ENC_TEST_VECTORS);
		test_cipher("ecb(xtea)", DECRYPT, xtea_dec_tv_template,
			    XTEA_DEC_TEST_VECTORS);
		break;

	case 21:
		test_cipher("ecb(khazad)", ENCRYPT, khazad_enc_tv_template,
			    KHAZAD_ENC_TEST_VECTORS);
		test_cipher("ecb(khazad)", DECRYPT, khazad_dec_tv_template,
			    KHAZAD_DEC_TEST_VECTORS);
		break;

	case 22:
		test_hash("wp512", wp512_tv_template, WP512_TEST_VECTORS);
		break;

	case 23:
		test_hash("wp384", wp384_tv_template, WP384_TEST_VECTORS);
		break;

	case 24:
		test_hash("wp256", wp256_tv_template, WP256_TEST_VECTORS);
		break;

	case 25:
		test_cipher("ecb(tnepres)", ENCRYPT, tnepres_enc_tv_template,
			    TNEPRES_ENC_TEST_VECTORS);
		test_cipher("ecb(tnepres)", DECRYPT, tnepres_dec_tv_template,
			    TNEPRES_DEC_TEST_VECTORS);
		break;

	case 26:
		test_cipher("ecb(anubis)", ENCRYPT, anubis_enc_tv_template,
			    ANUBIS_ENC_TEST_VECTORS);
		test_cipher("ecb(anubis)", DECRYPT, anubis_dec_tv_template,
			    ANUBIS_DEC_TEST_VECTORS);
		test_cipher("cbc(anubis)", ENCRYPT, anubis_cbc_enc_tv_template,
			    ANUBIS_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(anubis)", DECRYPT, anubis_cbc_dec_tv_template,
			    ANUBIS_CBC_ENC_TEST_VECTORS);
		break;

	case 27:
		test_hash("tgr192", tgr192_tv_template, TGR192_TEST_VECTORS);
		break;

	case 28:

		test_hash("tgr160", tgr160_tv_template, TGR160_TEST_VECTORS);
		break;

	case 29:
		test_hash("tgr128", tgr128_tv_template, TGR128_TEST_VECTORS);
		break;

	case 30:
		test_cipher("ecb(xeta)", ENCRYPT, xeta_enc_tv_template,
			    XETA_ENC_TEST_VECTORS);
		test_cipher("ecb(xeta)", DECRYPT, xeta_dec_tv_template,
			    XETA_DEC_TEST_VECTORS);
		break;

	case 31:
		test_cipher("pcbc(fcrypt)", ENCRYPT, fcrypt_pcbc_enc_tv_template,
			    FCRYPT_ENC_TEST_VECTORS);
		test_cipher("pcbc(fcrypt)", DECRYPT, fcrypt_pcbc_dec_tv_template,
			    FCRYPT_DEC_TEST_VECTORS);
		break;

	case 32:
		test_cipher("ecb(camellia)", ENCRYPT,
			    camellia_enc_tv_template,
			    CAMELLIA_ENC_TEST_VECTORS);
		test_cipher("ecb(camellia)", DECRYPT,
			    camellia_dec_tv_template,
			    CAMELLIA_DEC_TEST_VECTORS);
		test_cipher("cbc(camellia)", ENCRYPT,
			    camellia_cbc_enc_tv_template,
			    CAMELLIA_CBC_ENC_TEST_VECTORS);
		test_cipher("cbc(camellia)", DECRYPT,
			    camellia_cbc_dec_tv_template,
			    CAMELLIA_CBC_DEC_TEST_VECTORS);
		break;
	case 33:
		test_hash("sha224", sha224_tv_template, SHA224_TEST_VECTORS);
		break;

	case 34:
		test_cipher("salsa20", ENCRYPT,
			    salsa20_stream_enc_tv_template,
			    SALSA20_STREAM_ENC_TEST_VECTORS);
		break;

	case 35:
		test_aead("gcm(aes)", ENCRYPT, aes_gcm_enc_tv_template,
			  AES_GCM_ENC_TEST_VECTORS);
		test_aead("gcm(aes)", DECRYPT, aes_gcm_dec_tv_template,
			  AES_GCM_DEC_TEST_VECTORS);
		break;

	case 36:
		test_comp("lzo", lzo_comp_tv_template, lzo_decomp_tv_template,
			  LZO_COMP_TEST_VECTORS, LZO_DECOMP_TEST_VECTORS);
		break;

	case 37:
		test_aead("ccm(aes)", ENCRYPT, aes_ccm_enc_tv_template,
			  AES_CCM_ENC_TEST_VECTORS);
		test_aead("ccm(aes)", DECRYPT, aes_ccm_dec_tv_template,
			  AES_CCM_DEC_TEST_VECTORS);
		break;

	case 38:
		test_cipher("cts(cbc(aes))", ENCRYPT, cts_mode_enc_tv_template,
			    CTS_MODE_ENC_TEST_VECTORS);
		test_cipher("cts(cbc(aes))", DECRYPT, cts_mode_dec_tv_template,
			    CTS_MODE_DEC_TEST_VECTORS);
		break;

        case 39:
		test_hash("rmd128", rmd128_tv_template, RMD128_TEST_VECTORS);
		break;

        case 40:
		test_hash("rmd160", rmd160_tv_template, RMD160_TEST_VECTORS);
		break;

	case 41:
		test_hash("rmd256", rmd256_tv_template, RMD256_TEST_VECTORS);
		break;

	case 42:
		test_hash("rmd320", rmd320_tv_template, RMD320_TEST_VECTORS);
		break;

	case 100:
		test_hash("hmac(md5)", hmac_md5_tv_template,
			  HMAC_MD5_TEST_VECTORS);
		break;

	case 101:
		test_hash("hmac(sha1)", hmac_sha1_tv_template,
			  HMAC_SHA1_TEST_VECTORS);
		break;

	case 102:
		test_hash("hmac(sha256)", hmac_sha256_tv_template,
			  HMAC_SHA256_TEST_VECTORS);
		break;

	case 103:
		test_hash("hmac(sha384)", hmac_sha384_tv_template,
			  HMAC_SHA384_TEST_VECTORS);
		break;

	case 104:
		test_hash("hmac(sha512)", hmac_sha512_tv_template,
			  HMAC_SHA512_TEST_VECTORS);
		break;

	case 105:
		test_hash("hmac(sha224)", hmac_sha224_tv_template,
			  HMAC_SHA224_TEST_VECTORS);
		break;

	case 106:
		test_hash("xcbc(aes)", aes_xcbc128_tv_template,
			  XCBC_AES_TEST_VECTORS);
		break;

	case 107:
		test_hash("hmac(rmd128)", hmac_rmd128_tv_template,
			  HMAC_RMD128_TEST_VECTORS);
		break;

	case 108:
		test_hash("hmac(rmd160)", hmac_rmd160_tv_template,
			  HMAC_RMD160_TEST_VECTORS);
		break;

static inline int tcrypt_test(const char *alg)
{
	int ret;

	ret = alg_test(alg, alg, 0, 0);
	/* non-fips algs return -EINVAL in fips mode */
	if (fips_enabled && ret == -EINVAL)
		ret = 0;
	return ret;
}

static int do_test(const char *alg, u32 type, u32 mask, int m)
{
	int i;
	int ret = 0;

	switch (m) {
	case 0:
		if (alg) {
			if (!crypto_has_alg(alg, type,
					    mask ?: CRYPTO_ALG_TYPE_MASK))
				ret = -ENOENT;
			break;
		}

		for (i = 1; i < 200; i++)
			ret += do_test(NULL, 0, 0, i);
		break;

	case 1:
		ret += tcrypt_test("md5");
		break;

	case 2:
		ret += tcrypt_test("sha1");
		break;

	case 3:
		ret += tcrypt_test("ecb(des)");
		ret += tcrypt_test("cbc(des)");
		ret += tcrypt_test("ctr(des)");
		break;

	case 4:
		ret += tcrypt_test("ecb(des3_ede)");
		ret += tcrypt_test("cbc(des3_ede)");
		ret += tcrypt_test("ctr(des3_ede)");
		break;

	case 5:
		ret += tcrypt_test("md4");
		break;

	case 6:
		ret += tcrypt_test("sha256");
		break;

	case 7:
		ret += tcrypt_test("ecb(blowfish)");
		ret += tcrypt_test("cbc(blowfish)");
		ret += tcrypt_test("ctr(blowfish)");
		break;

	case 8:
		ret += tcrypt_test("ecb(twofish)");
		ret += tcrypt_test("cbc(twofish)");
		ret += tcrypt_test("ctr(twofish)");
		ret += tcrypt_test("lrw(twofish)");
		ret += tcrypt_test("xts(twofish)");
		break;

	case 9:
		ret += tcrypt_test("ecb(serpent)");
		ret += tcrypt_test("cbc(serpent)");
		ret += tcrypt_test("ctr(serpent)");
		ret += tcrypt_test("lrw(serpent)");
		ret += tcrypt_test("xts(serpent)");
		break;

	case 10:
		ret += tcrypt_test("ecb(aes)");
		ret += tcrypt_test("cbc(aes)");
		ret += tcrypt_test("lrw(aes)");
		ret += tcrypt_test("xts(aes)");
		ret += tcrypt_test("ctr(aes)");
		ret += tcrypt_test("rfc3686(ctr(aes))");
		break;

	case 11:
		ret += tcrypt_test("sha384");
		break;

	case 12:
		ret += tcrypt_test("sha512");
		break;

	case 13:
		ret += tcrypt_test("deflate");
		break;

	case 14:
		ret += tcrypt_test("ecb(cast5)");
		ret += tcrypt_test("cbc(cast5)");
		ret += tcrypt_test("ctr(cast5)");
		break;

	case 15:
		ret += tcrypt_test("ecb(cast6)");
		ret += tcrypt_test("cbc(cast6)");
		ret += tcrypt_test("ctr(cast6)");
		ret += tcrypt_test("lrw(cast6)");
		ret += tcrypt_test("xts(cast6)");
		break;

	case 16:
		ret += tcrypt_test("ecb(arc4)");
		break;

	case 17:
		ret += tcrypt_test("michael_mic");
		break;

	case 18:
		ret += tcrypt_test("crc32c");
		break;

	case 19:
		ret += tcrypt_test("ecb(tea)");
		break;

	case 20:
		ret += tcrypt_test("ecb(xtea)");
		break;

	case 21:
		ret += tcrypt_test("ecb(khazad)");
		break;

	case 22:
		ret += tcrypt_test("wp512");
		break;

	case 23:
		ret += tcrypt_test("wp384");
		break;

	case 24:
		ret += tcrypt_test("wp256");
		break;

	case 25:
		ret += tcrypt_test("ecb(tnepres)");
		break;

	case 26:
		ret += tcrypt_test("ecb(anubis)");
		ret += tcrypt_test("cbc(anubis)");
		break;

	case 27:
		ret += tcrypt_test("tgr192");
		break;

	case 28:
		ret += tcrypt_test("tgr160");
		break;

	case 29:
		ret += tcrypt_test("tgr128");
		break;

	case 30:
		ret += tcrypt_test("ecb(xeta)");
		break;

	case 31:
		ret += tcrypt_test("pcbc(fcrypt)");
		break;

	case 32:
		ret += tcrypt_test("ecb(camellia)");
		ret += tcrypt_test("cbc(camellia)");
		ret += tcrypt_test("ctr(camellia)");
		ret += tcrypt_test("lrw(camellia)");
		ret += tcrypt_test("xts(camellia)");
		break;

	case 33:
		ret += tcrypt_test("sha224");
		break;

	case 34:
		ret += tcrypt_test("salsa20");
		break;

	case 35:
		ret += tcrypt_test("gcm(aes)");
		break;

	case 36:
		ret += tcrypt_test("lzo");
		break;

	case 37:
		ret += tcrypt_test("ccm(aes)");
		break;

	case 38:
		ret += tcrypt_test("cts(cbc(aes))");
		break;

        case 39:
		ret += tcrypt_test("rmd128");
		break;

        case 40:
		ret += tcrypt_test("rmd160");
		break;

	case 41:
		ret += tcrypt_test("rmd256");
		break;

	case 42:
		ret += tcrypt_test("rmd320");
		break;

	case 43:
		ret += tcrypt_test("ecb(seed)");
		break;

	case 44:
		ret += tcrypt_test("zlib");
		break;

	case 45:
		ret += tcrypt_test("rfc4309(ccm(aes))");
		break;

	case 46:
		ret += tcrypt_test("ghash");
		break;

	case 47:
		ret += tcrypt_test("crct10dif");
		break;

	case 100:
		ret += tcrypt_test("hmac(md5)");
		break;

	case 101:
		ret += tcrypt_test("hmac(sha1)");
		break;

	case 102:
		ret += tcrypt_test("hmac(sha256)");
		break;

	case 103:
		ret += tcrypt_test("hmac(sha384)");
		break;

	case 104:
		ret += tcrypt_test("hmac(sha512)");
		break;

	case 105:
		ret += tcrypt_test("hmac(sha224)");
		break;

	case 106:
		ret += tcrypt_test("xcbc(aes)");
		break;

	case 107:
		ret += tcrypt_test("hmac(rmd128)");
		break;

	case 108:
		ret += tcrypt_test("hmac(rmd160)");
		break;

	case 109:
		ret += tcrypt_test("vmac(aes)");
		break;

	case 110:
		ret += tcrypt_test("hmac(crc32)");
		break;

	case 150:
		ret += tcrypt_test("ansi_cprng");
		break;

	case 151:
		ret += tcrypt_test("rfc4106(gcm(aes))");
		break;

	case 152:
		ret += tcrypt_test("rfc4543(gcm(aes))");
		break;

	case 153:
		ret += tcrypt_test("cmac(aes)");
		break;

	case 154:
		ret += tcrypt_test("cmac(des3_ede)");
		break;

	case 155:
		ret += tcrypt_test("authenc(hmac(sha1),cbc(aes))");
		break;

	case 156:
		ret += tcrypt_test("authenc(hmac(md5),ecb(cipher_null))");
		break;

	case 157:
		ret += tcrypt_test("authenc(hmac(sha1),ecb(cipher_null))");
		break;
	case 181:
		ret += tcrypt_test("authenc(hmac(sha1),cbc(des))");
		break;
	case 182:
		ret += tcrypt_test("authenc(hmac(sha1),cbc(des3_ede))");
		break;
	case 183:
		ret += tcrypt_test("authenc(hmac(sha224),cbc(des))");
		break;
	case 184:
		ret += tcrypt_test("authenc(hmac(sha224),cbc(des3_ede))");
		break;
	case 185:
		ret += tcrypt_test("authenc(hmac(sha256),cbc(des))");
		break;
	case 186:
		ret += tcrypt_test("authenc(hmac(sha256),cbc(des3_ede))");
		break;
	case 187:
		ret += tcrypt_test("authenc(hmac(sha384),cbc(des))");
		break;
	case 188:
		ret += tcrypt_test("authenc(hmac(sha384),cbc(des3_ede))");
		break;
	case 189:
		ret += tcrypt_test("authenc(hmac(sha512),cbc(des))");
		break;
	case 190:
		ret += tcrypt_test("authenc(hmac(sha512),cbc(des3_ede))");
		break;
	case 200:
		test_cipher_speed("ecb(aes)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("ecb(aes)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("cbc(aes)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("cbc(aes)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("lrw(aes)", ENCRYPT, sec, NULL, 0,
				speed_template_32_40_48);
		test_cipher_speed("lrw(aes)", DECRYPT, sec, NULL, 0,
				speed_template_32_40_48);
		test_cipher_speed("xts(aes)", ENCRYPT, sec, NULL, 0,
				speed_template_32_48_64);
		test_cipher_speed("xts(aes)", DECRYPT, sec, NULL, 0,
				speed_template_32_48_64);
		test_cipher_speed("ctr(aes)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("ctr(aes)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		break;

	case 201:
		test_cipher_speed("ecb(des3_ede)", ENCRYPT, sec,
				des3_ede_enc_tv_template, DES3_EDE_ENC_TEST_VECTORS,
				speed_template_24);
		test_cipher_speed("ecb(des3_ede)", DECRYPT, sec,
				des3_ede_enc_tv_template, DES3_EDE_ENC_TEST_VECTORS,
				speed_template_24);
		test_cipher_speed("cbc(des3_ede)", ENCRYPT, sec,
				des3_ede_enc_tv_template, DES3_EDE_ENC_TEST_VECTORS,
				speed_template_24);
		test_cipher_speed("cbc(des3_ede)", DECRYPT, sec,
				des3_ede_enc_tv_template, DES3_EDE_ENC_TEST_VECTORS,
				des3_speed_template, DES3_SPEED_VECTORS,
				speed_template_24);
		test_cipher_speed("ecb(des3_ede)", DECRYPT, sec,
				des3_speed_template, DES3_SPEED_VECTORS,
				speed_template_24);
		test_cipher_speed("cbc(des3_ede)", ENCRYPT, sec,
				des3_speed_template, DES3_SPEED_VECTORS,
				speed_template_24);
		test_cipher_speed("cbc(des3_ede)", DECRYPT, sec,
				des3_speed_template, DES3_SPEED_VECTORS,
				speed_template_24);
		test_cipher_speed("ctr(des3_ede)", ENCRYPT, sec,
				des3_speed_template, DES3_SPEED_VECTORS,
				speed_template_24);
		test_cipher_speed("ctr(des3_ede)", DECRYPT, sec,
				des3_speed_template, DES3_SPEED_VECTORS,
				speed_template_24);
		break;

	case 202:
		test_cipher_speed("ecb(twofish)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("ecb(twofish)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("cbc(twofish)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("cbc(twofish)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("ctr(twofish)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("ctr(twofish)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("lrw(twofish)", ENCRYPT, sec, NULL, 0,
				speed_template_32_40_48);
		test_cipher_speed("lrw(twofish)", DECRYPT, sec, NULL, 0,
				speed_template_32_40_48);
		test_cipher_speed("xts(twofish)", ENCRYPT, sec, NULL, 0,
				speed_template_32_48_64);
		test_cipher_speed("xts(twofish)", DECRYPT, sec, NULL, 0,
				speed_template_32_48_64);
		break;

	case 203:
		test_cipher_speed("ecb(blowfish)", ENCRYPT, sec, NULL, 0,
				  speed_template_8_32);
		test_cipher_speed("ecb(blowfish)", DECRYPT, sec, NULL, 0,
				  speed_template_8_32);
		test_cipher_speed("cbc(blowfish)", ENCRYPT, sec, NULL, 0,
				  speed_template_8_32);
		test_cipher_speed("cbc(blowfish)", DECRYPT, sec, NULL, 0,
				  speed_template_8_32);
		test_cipher_speed("ctr(blowfish)", ENCRYPT, sec, NULL, 0,
				  speed_template_8_32);
		test_cipher_speed("ctr(blowfish)", DECRYPT, sec, NULL, 0,
				  speed_template_8_32);
		break;

	case 204:
		test_cipher_speed("ecb(des)", ENCRYPT, sec, NULL, 0,
				  speed_template_8);
		test_cipher_speed("ecb(des)", DECRYPT, sec, NULL, 0,
				  speed_template_8);
		test_cipher_speed("cbc(des)", ENCRYPT, sec, NULL, 0,
				  speed_template_8);
		test_cipher_speed("cbc(des)", DECRYPT, sec, NULL, 0,
				  speed_template_8);
		break;

	case 205:
		test_cipher_speed("ecb(camellia)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("ecb(camellia)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("cbc(camellia)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("cbc(camellia)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("ctr(camellia)", ENCRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("ctr(camellia)", DECRYPT, sec, NULL, 0,
				speed_template_16_24_32);
		test_cipher_speed("lrw(camellia)", ENCRYPT, sec, NULL, 0,
				speed_template_32_40_48);
		test_cipher_speed("lrw(camellia)", DECRYPT, sec, NULL, 0,
				speed_template_32_40_48);
		test_cipher_speed("xts(camellia)", ENCRYPT, sec, NULL, 0,
				speed_template_32_48_64);
		test_cipher_speed("xts(camellia)", DECRYPT, sec, NULL, 0,
				speed_template_32_48_64);
		break;

	case 206:
		test_cipher_speed("salsa20", ENCRYPT, sec, NULL, 0,
				  speed_template_16_32);
		break;

	case 300:
	case 207:
		test_cipher_speed("ecb(serpent)", ENCRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("ecb(serpent)", DECRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("cbc(serpent)", ENCRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("cbc(serpent)", DECRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("ctr(serpent)", ENCRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("ctr(serpent)", DECRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("lrw(serpent)", ENCRYPT, sec, NULL, 0,
				  speed_template_32_48);
		test_cipher_speed("lrw(serpent)", DECRYPT, sec, NULL, 0,
				  speed_template_32_48);
		test_cipher_speed("xts(serpent)", ENCRYPT, sec, NULL, 0,
				  speed_template_32_64);
		test_cipher_speed("xts(serpent)", DECRYPT, sec, NULL, 0,
				  speed_template_32_64);
		break;

	case 208:
		test_cipher_speed("ecb(arc4)", ENCRYPT, sec, NULL, 0,
				  speed_template_8);
		break;

	case 209:
		test_cipher_speed("ecb(cast5)", ENCRYPT, sec, NULL, 0,
				  speed_template_8_16);
		test_cipher_speed("ecb(cast5)", DECRYPT, sec, NULL, 0,
				  speed_template_8_16);
		test_cipher_speed("cbc(cast5)", ENCRYPT, sec, NULL, 0,
				  speed_template_8_16);
		test_cipher_speed("cbc(cast5)", DECRYPT, sec, NULL, 0,
				  speed_template_8_16);
		test_cipher_speed("ctr(cast5)", ENCRYPT, sec, NULL, 0,
				  speed_template_8_16);
		test_cipher_speed("ctr(cast5)", DECRYPT, sec, NULL, 0,
				  speed_template_8_16);
		break;

	case 210:
		test_cipher_speed("ecb(cast6)", ENCRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("ecb(cast6)", DECRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("cbc(cast6)", ENCRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("cbc(cast6)", DECRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("ctr(cast6)", ENCRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("ctr(cast6)", DECRYPT, sec, NULL, 0,
				  speed_template_16_32);
		test_cipher_speed("lrw(cast6)", ENCRYPT, sec, NULL, 0,
				  speed_template_32_48);
		test_cipher_speed("lrw(cast6)", DECRYPT, sec, NULL, 0,
				  speed_template_32_48);
		test_cipher_speed("xts(cast6)", ENCRYPT, sec, NULL, 0,
				  speed_template_32_64);
		test_cipher_speed("xts(cast6)", DECRYPT, sec, NULL, 0,
				  speed_template_32_64);
		break;

	case 211:
		test_aead_speed("rfc4106(gcm(aes))", ENCRYPT, sec,
				NULL, 0, 16, 16, aead_speed_template_20);
		test_aead_speed("gcm(aes)", ENCRYPT, sec,
				NULL, 0, 16, 8, aead_speed_template_20);
		break;

	case 212:
		test_aead_speed("rfc4309(ccm(aes))", ENCRYPT, sec,
				NULL, 0, 16, 16, aead_speed_template_19);
		break;

	case 213:
		test_aead_speed("rfc7539esp(chacha20,poly1305)", ENCRYPT, sec,
				NULL, 0, 16, 8, aead_speed_template_36);
		break;

	case 214:
		test_cipher_speed("chacha20", ENCRYPT, sec, NULL, 0,
				  speed_template_32);
		break;


	case 300:
		if (alg) {
			test_hash_speed(alg, sec, generic_hash_speed_template);
			break;
		}

		/* fall through */

	case 301:
		test_hash_speed("md4", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 302:
		test_hash_speed("md5", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 303:
		test_hash_speed("sha1", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 304:
		test_hash_speed("sha256", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 305:
		test_hash_speed("sha384", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 306:
		test_hash_speed("sha512", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 307:
		test_hash_speed("wp256", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 308:
		test_hash_speed("wp384", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 309:
		test_hash_speed("wp512", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 310:
		test_hash_speed("tgr128", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 311:
		test_hash_speed("tgr160", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 312:
		test_hash_speed("tgr192", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 313:
		test_hash_speed("sha224", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 314:
		test_hash_speed("rmd128", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 315:
		test_hash_speed("rmd160", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 316:
		test_hash_speed("rmd256", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 317:
		test_hash_speed("rmd320", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 399:
		break;

	case 1000:
		test_available();
		break;

	default:
		/* useful for debugging */
		printk("not testing anything\n");
		break;
	}
	case 318:
		test_hash_speed("ghash-generic", sec, hash_speed_template_16);
		if (mode > 300 && mode < 400) break;

	case 319:
		test_hash_speed("crc32c", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 320:
		test_hash_speed("crct10dif", sec, generic_hash_speed_template);
		if (mode > 300 && mode < 400) break;

	case 321:
		test_hash_speed("poly1305", sec, poly1305_speed_template);
		if (mode > 300 && mode < 400) break;

	case 399:
		break;

	case 400:
		if (alg) {
			test_ahash_speed(alg, sec, generic_hash_speed_template);
			break;
		}

		/* fall through */

	case 401:
		test_ahash_speed("md4", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 402:
		test_ahash_speed("md5", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 403:
		test_ahash_speed("sha1", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 404:
		test_ahash_speed("sha256", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 405:
		test_ahash_speed("sha384", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 406:
		test_ahash_speed("sha512", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 407:
		test_ahash_speed("wp256", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 408:
		test_ahash_speed("wp384", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 409:
		test_ahash_speed("wp512", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 410:
		test_ahash_speed("tgr128", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 411:
		test_ahash_speed("tgr160", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 412:
		test_ahash_speed("tgr192", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 413:
		test_ahash_speed("sha224", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 414:
		test_ahash_speed("rmd128", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 415:
		test_ahash_speed("rmd160", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 416:
		test_ahash_speed("rmd256", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 417:
		test_ahash_speed("rmd320", sec, generic_hash_speed_template);
		if (mode > 400 && mode < 500) break;

	case 499:
		break;

	case 500:
		test_acipher_speed("ecb(aes)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("ecb(aes)", DECRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("cbc(aes)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("cbc(aes)", DECRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("lrw(aes)", ENCRYPT, sec, NULL, 0,
				   speed_template_32_40_48);
		test_acipher_speed("lrw(aes)", DECRYPT, sec, NULL, 0,
				   speed_template_32_40_48);
		test_acipher_speed("xts(aes)", ENCRYPT, sec, NULL, 0,
				   speed_template_32_48_64);
		test_acipher_speed("xts(aes)", DECRYPT, sec, NULL, 0,
				   speed_template_32_48_64);
		test_acipher_speed("ctr(aes)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("ctr(aes)", DECRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("cfb(aes)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("cfb(aes)", DECRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("ofb(aes)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("ofb(aes)", DECRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("rfc3686(ctr(aes))", ENCRYPT, sec, NULL, 0,
				   speed_template_20_28_36);
		test_acipher_speed("rfc3686(ctr(aes))", DECRYPT, sec, NULL, 0,
				   speed_template_20_28_36);
		break;

	case 501:
		test_acipher_speed("ecb(des3_ede)", ENCRYPT, sec,
				   des3_speed_template, DES3_SPEED_VECTORS,
				   speed_template_24);
		test_acipher_speed("ecb(des3_ede)", DECRYPT, sec,
				   des3_speed_template, DES3_SPEED_VECTORS,
				   speed_template_24);
		test_acipher_speed("cbc(des3_ede)", ENCRYPT, sec,
				   des3_speed_template, DES3_SPEED_VECTORS,
				   speed_template_24);
		test_acipher_speed("cbc(des3_ede)", DECRYPT, sec,
				   des3_speed_template, DES3_SPEED_VECTORS,
				   speed_template_24);
		test_acipher_speed("cfb(des3_ede)", ENCRYPT, sec,
				   des3_speed_template, DES3_SPEED_VECTORS,
				   speed_template_24);
		test_acipher_speed("cfb(des3_ede)", DECRYPT, sec,
				   des3_speed_template, DES3_SPEED_VECTORS,
				   speed_template_24);
		test_acipher_speed("ofb(des3_ede)", ENCRYPT, sec,
				   des3_speed_template, DES3_SPEED_VECTORS,
				   speed_template_24);
		test_acipher_speed("ofb(des3_ede)", DECRYPT, sec,
				   des3_speed_template, DES3_SPEED_VECTORS,
				   speed_template_24);
		break;

	case 502:
		test_acipher_speed("ecb(des)", ENCRYPT, sec, NULL, 0,
				   speed_template_8);
		test_acipher_speed("ecb(des)", DECRYPT, sec, NULL, 0,
				   speed_template_8);
		test_acipher_speed("cbc(des)", ENCRYPT, sec, NULL, 0,
				   speed_template_8);
		test_acipher_speed("cbc(des)", DECRYPT, sec, NULL, 0,
				   speed_template_8);
		test_acipher_speed("cfb(des)", ENCRYPT, sec, NULL, 0,
				   speed_template_8);
		test_acipher_speed("cfb(des)", DECRYPT, sec, NULL, 0,
				   speed_template_8);
		test_acipher_speed("ofb(des)", ENCRYPT, sec, NULL, 0,
				   speed_template_8);
		test_acipher_speed("ofb(des)", DECRYPT, sec, NULL, 0,
				   speed_template_8);
		break;

	case 503:
		test_acipher_speed("ecb(serpent)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("ecb(serpent)", DECRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("cbc(serpent)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("cbc(serpent)", DECRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("ctr(serpent)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("ctr(serpent)", DECRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("lrw(serpent)", ENCRYPT, sec, NULL, 0,
				   speed_template_32_48);
		test_acipher_speed("lrw(serpent)", DECRYPT, sec, NULL, 0,
				   speed_template_32_48);
		test_acipher_speed("xts(serpent)", ENCRYPT, sec, NULL, 0,
				   speed_template_32_64);
		test_acipher_speed("xts(serpent)", DECRYPT, sec, NULL, 0,
				   speed_template_32_64);
		break;

	case 504:
		test_acipher_speed("ecb(twofish)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("ecb(twofish)", DECRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("cbc(twofish)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("cbc(twofish)", DECRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("ctr(twofish)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("ctr(twofish)", DECRYPT, sec, NULL, 0,
				   speed_template_16_24_32);
		test_acipher_speed("lrw(twofish)", ENCRYPT, sec, NULL, 0,
				   speed_template_32_40_48);
		test_acipher_speed("lrw(twofish)", DECRYPT, sec, NULL, 0,
				   speed_template_32_40_48);
		test_acipher_speed("xts(twofish)", ENCRYPT, sec, NULL, 0,
				   speed_template_32_48_64);
		test_acipher_speed("xts(twofish)", DECRYPT, sec, NULL, 0,
				   speed_template_32_48_64);
		break;

	case 505:
		test_acipher_speed("ecb(arc4)", ENCRYPT, sec, NULL, 0,
				   speed_template_8);
		break;

	case 506:
		test_acipher_speed("ecb(cast5)", ENCRYPT, sec, NULL, 0,
				   speed_template_8_16);
		test_acipher_speed("ecb(cast5)", DECRYPT, sec, NULL, 0,
				   speed_template_8_16);
		test_acipher_speed("cbc(cast5)", ENCRYPT, sec, NULL, 0,
				   speed_template_8_16);
		test_acipher_speed("cbc(cast5)", DECRYPT, sec, NULL, 0,
				   speed_template_8_16);
		test_acipher_speed("ctr(cast5)", ENCRYPT, sec, NULL, 0,
				   speed_template_8_16);
		test_acipher_speed("ctr(cast5)", DECRYPT, sec, NULL, 0,
				   speed_template_8_16);
		break;

	case 507:
		test_acipher_speed("ecb(cast6)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("ecb(cast6)", DECRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("cbc(cast6)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("cbc(cast6)", DECRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("ctr(cast6)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("ctr(cast6)", DECRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("lrw(cast6)", ENCRYPT, sec, NULL, 0,
				   speed_template_32_48);
		test_acipher_speed("lrw(cast6)", DECRYPT, sec, NULL, 0,
				   speed_template_32_48);
		test_acipher_speed("xts(cast6)", ENCRYPT, sec, NULL, 0,
				   speed_template_32_64);
		test_acipher_speed("xts(cast6)", DECRYPT, sec, NULL, 0,
				   speed_template_32_64);
		break;

	case 508:
		test_acipher_speed("ecb(camellia)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("ecb(camellia)", DECRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("cbc(camellia)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("cbc(camellia)", DECRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("ctr(camellia)", ENCRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("ctr(camellia)", DECRYPT, sec, NULL, 0,
				   speed_template_16_32);
		test_acipher_speed("lrw(camellia)", ENCRYPT, sec, NULL, 0,
				   speed_template_32_48);
		test_acipher_speed("lrw(camellia)", DECRYPT, sec, NULL, 0,
				   speed_template_32_48);
		test_acipher_speed("xts(camellia)", ENCRYPT, sec, NULL, 0,
				   speed_template_32_64);
		test_acipher_speed("xts(camellia)", DECRYPT, sec, NULL, 0,
				   speed_template_32_64);
		break;

	case 509:
		test_acipher_speed("ecb(blowfish)", ENCRYPT, sec, NULL, 0,
				   speed_template_8_32);
		test_acipher_speed("ecb(blowfish)", DECRYPT, sec, NULL, 0,
				   speed_template_8_32);
		test_acipher_speed("cbc(blowfish)", ENCRYPT, sec, NULL, 0,
				   speed_template_8_32);
		test_acipher_speed("cbc(blowfish)", DECRYPT, sec, NULL, 0,
				   speed_template_8_32);
		test_acipher_speed("ctr(blowfish)", ENCRYPT, sec, NULL, 0,
				   speed_template_8_32);
		test_acipher_speed("ctr(blowfish)", DECRYPT, sec, NULL, 0,
				   speed_template_8_32);
		break;

	case 1000:
		test_available();
		break;
	}

	return ret;
}

static int __init tcrypt_mod_init(void)
{
	int err = -ENOMEM;

	tvmem = kmalloc(TVMEMSIZE, GFP_KERNEL);
	if (tvmem == NULL)
		return err;

	xbuf = kmalloc(XBUFSIZE, GFP_KERNEL);
	if (xbuf == NULL)
		goto err_free_tv;

	axbuf = kmalloc(XBUFSIZE, GFP_KERNEL);
	if (axbuf == NULL)
		goto err_free_xbuf;

	do_test();

	/* We intentionaly return -EAGAIN to prevent keeping
	 * the module. It does all its work from init()
	 * and doesn't offer any runtime functionality 
	 * => we don't need it in the memory, do we?
	 *                                        -- mludvig
	 */
	err = -EAGAIN;

	kfree(axbuf);
 err_free_xbuf:
	kfree(xbuf);
 err_free_tv:
	kfree(tvmem);
	int i;

	for (i = 0; i < TVMEMSIZE; i++) {
		tvmem[i] = (void *)__get_free_page(GFP_KERNEL);
		if (!tvmem[i])
			goto err_free_tv;
	}

	err = do_test(alg, type, mask, mode);

	if (err) {
		printk(KERN_ERR "tcrypt: one or more tests failed!\n");
		goto err_free_tv;
	}

	/* We intentionaly return -EAGAIN to prevent keeping the module,
	 * unless we're running in fips mode. It does all its work from
	 * init() and doesn't offer any runtime functionality, but in
	 * the fips case, checking for a successful load is helpful.
	 * => we don't need it in the memory, do we?
	 *                                        -- mludvig
	 */
	if (!fips_enabled)
		err = -EAGAIN;

err_free_tv:
	for (i = 0; i < TVMEMSIZE && tvmem[i]; i++)
		free_page((unsigned long)tvmem[i]);

	return err;
}

/*
 * If an init function is provided, an exit function must also be provided
 * to allow module unload.
 */
static void __exit tcrypt_mod_fini(void) { }

module_init(tcrypt_mod_init);
module_exit(tcrypt_mod_fini);

module_param(alg, charp, 0);
module_param(type, uint, 0);
module_param(mask, uint, 0);
module_param(mode, int, 0);
module_param(sec, uint, 0);
MODULE_PARM_DESC(sec, "Length in seconds of speed tests "
		      "(defaults to zero which uses CPU cycles instead)");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Quick & dirty crypto testing module");
MODULE_AUTHOR("James Morris <jmorris@intercode.com.au>");
