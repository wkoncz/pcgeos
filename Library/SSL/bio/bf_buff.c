/* crypto/bio/bf_buff.c */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 * 
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 * 
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from 
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 * 
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#ifndef COMPILE_OPTION_HOST_SERVICE_ONLY

#ifdef __GEOS__
#include <Ansi/stdio.h>
#else
#include <stdio.h>
#endif
#include <errno.h>
#include "cryptlib.h"
#include "bio.h"
#include "evp.h"

#ifndef NOPROTO
static int buffer_write(BIO *h,char *buf,int num);
static int buffer_read(BIO *h,char *buf,int size);
static int buffer_puts(BIO *h,char *str);
static int buffer_gets(BIO *h,char *str,int size);
static long buffer_ctrl(BIO *h,int cmd,long arg1,char *arg2);
static int buffer_new(BIO *h);
static int buffer_free(BIO *data);
#else
static int buffer_write();
static int buffer_read();
static int buffer_puts();
static int buffer_gets();
static long buffer_ctrl();
static int buffer_new();
static int buffer_free();
#endif

#define DEFAULT_BUFFER_SIZE	1024

static BIO_METHOD methods_buffer=
	{
	BIO_TYPE_BUFFER,
	"buffer",
	buffer_write,
	buffer_read,
	buffer_puts,
	buffer_gets,
	buffer_ctrl,
	buffer_new,
	buffer_free,
	};

BIO_METHOD *BIO_f_buffer()
	{
#ifdef __GEOS__
	BIO_METHOD *ret;
	PUSHDS;
	ret = &methods_buffer;
	POPDS;
	return(ret);
#else
	return(&methods_buffer);
#endif
	}

static int buffer_new(bi)
BIO *bi;
	{
	BIO_F_BUFFER_CTX *ctx;

	ctx=(BIO_F_BUFFER_CTX *)Malloc(sizeof(BIO_F_BUFFER_CTX));
	if (ctx == NULL) return(0);
	ctx->ibuf=(char *)Malloc(DEFAULT_BUFFER_SIZE);
	if (ctx->ibuf == NULL) { Free(ctx); return(0); }
	ctx->obuf=(char *)Malloc(DEFAULT_BUFFER_SIZE);
	if (ctx->obuf == NULL) { Free(ctx->ibuf); Free(ctx); return(0); }
	ctx->ibuf_size=DEFAULT_BUFFER_SIZE;
	ctx->obuf_size=DEFAULT_BUFFER_SIZE;
	ctx->ibuf_len=0;
	ctx->ibuf_off=0;
	ctx->obuf_len=0;
	ctx->obuf_off=0;

	bi->init=1;
	bi->ptr=(char *)ctx;
	bi->flags=0;
	return(1);
	}

static int buffer_free(a)
BIO *a;
	{
	BIO_F_BUFFER_CTX *b;

	if (a == NULL) return(0);
	b=(BIO_F_BUFFER_CTX *)a->ptr;
	if (b->ibuf != NULL) Free(b->ibuf);
	if (b->obuf != NULL) Free(b->obuf);
	Free(a->ptr);
	a->ptr=NULL;
	a->init=0;
	a->flags=0;
	return(1);
	}
	
static int buffer_read(b,out,outl)
BIO *b;
char *out;
int outl;
	{
	int i,num=0;
	BIO_F_BUFFER_CTX *ctx;

	if (out == NULL) return(0);
	ctx=(BIO_F_BUFFER_CTX *)b->ptr;

	if ((ctx == NULL) || (b->next_bio == NULL)) return(0);
	num=0;
	BIO_clear_retry_flags(b);

start:
	i=ctx->ibuf_len;
	/* If there is stuff left over, grab it */
	if (i != 0)
		{
		if (i > outl) i=outl;
		memcpy(out,&(ctx->ibuf[ctx->ibuf_off]),i);
		ctx->ibuf_off+=i;
		ctx->ibuf_len-=i;
		num+=i;
		if (outl == i)  return(num);
		outl-=i;
		out+=i;
		}

	/* We may have done a partial read. try to do more.
	 * We have nothing in the buffer.
	 * If we get an error and have read some data, just return it
	 * and let them retry to get the error again.
	 * copy direct to parent address space */
	if (outl > ctx->ibuf_size)
		{
		for (;;)
			{
			i=BIO_read(b->next_bio,out,outl);
			if (i <= 0)
				{
				BIO_copy_next_retry(b);
				if (i < 0) return((num > 0)?num:i);
				if (i == 0) return(num);
				}
			num+=i;
			if (outl == i) return(num);
			out+=i;
			outl-=i;
			}
		}
	/* else */

	/* we are going to be doing some buffering */
	i=BIO_read(b->next_bio,ctx->ibuf,ctx->ibuf_size);
	if (i <= 0)
		{
		BIO_copy_next_retry(b);
		if (i < 0) return((num > 0)?num:i);
		if (i == 0) return(num);
		}
	ctx->ibuf_off=0;
	ctx->ibuf_len=i;

	/* Lets re-read using ourselves :-) */
	goto start;
	}

static int buffer_write(b,in,inl)
BIO *b;
char *in;
int inl;
	{
	int i,num=0;
	BIO_F_BUFFER_CTX *ctx;

	if ((in == NULL) || (inl <= 0)) return(0);
	ctx=(BIO_F_BUFFER_CTX *)b->ptr;
	if ((ctx == NULL) || (b->next_bio == NULL)) return(0);

	BIO_clear_retry_flags(b);
start:
	i=ctx->obuf_size-(ctx->obuf_len+ctx->obuf_off);
	/* add to buffer and return */
	if (i >= inl)
		{
		memcpy(&(ctx->obuf[ctx->obuf_len]),in,inl);
		ctx->obuf_len+=inl;
		return(num+inl);
		}
	/* else */
	/* stuff already in buffer, so add to it first, then flush */
	if (ctx->obuf_len != 0)
		{
		if (i > 0) /* lets fill it up if we can */
			{
			memcpy(&(ctx->obuf[ctx->obuf_len]),in,i);
			in+=i;
			inl-=i;
			num+=i;
			ctx->obuf_len+=i;
			}
		/* we now have a full buffer needing flushing */
		for (;;)
			{
			i=BIO_write(b->next_bio,&(ctx->obuf[ctx->obuf_off]),
				ctx->obuf_len);
			if (i <= 0)
				{
				BIO_copy_next_retry(b);

				if (i < 0) return((num > 0)?num:i);
				if (i == 0) return(num);
				}
			ctx->obuf_off+=i;
			ctx->obuf_len-=i;
			if (ctx->obuf_len == 0) break;
			}
		}
	/* we only get here if the buffer has been flushed and we
	 * still have stuff to write */
	ctx->obuf_off=0;

	/* we now have inl bytes to write */
	while (inl >= ctx->obuf_size)
		{
		i=BIO_write(b->next_bio,in,inl);
		if (i <= 0)
			{
			BIO_copy_next_retry(b);
			if (i < 0) return((num > 0)?num:i);
			if (i == 0) return(num);
			}
		num+=i;
		in+=i;
		inl-=i;
		if (inl == 0) return(num);
		}

	/* copy the rest into the buffer since we have only a small 
	 * amount left */
	goto start;
	}

static long buffer_ctrl(b,cmd,num,ptr)
BIO *b;
int cmd;
long num;
char *ptr;
	{
	BIO *dbio;
	BIO_F_BUFFER_CTX *ctx;
	long ret=1;
	char *p1,*p2;
	int r,i,*ip;
	int ibs,obs;

	ctx=(BIO_F_BUFFER_CTX *)b->ptr;

	switch (cmd)
		{
	case BIO_CTRL_RESET:
		ctx->ibuf_off=0;
		ctx->ibuf_len=0;
		ctx->obuf_off=0;
		ctx->obuf_len=0;
		ret=BIO_ctrl(b->next_bio,cmd,num,ptr);
		break;
	case BIO_CTRL_INFO:
		ret=(long)ctx->obuf_len;
		break;
	case BIO_C_GET_BUFF_NUM_LINES:
		ret=0;
		p1=ctx->ibuf;
		for (i=ctx->ibuf_off; i<ctx->ibuf_len; i++)
			{
			if (p1[i] == '\n') ret++;
			}
		break;
	case BIO_CTRL_WPENDING:
		ret=(long)ctx->obuf_len;
		if (ret == 0)
			ret=BIO_ctrl(b->next_bio,cmd,num,ptr);
		break;
	case BIO_CTRL_PENDING:
		ret=(long)ctx->ibuf_len;
		if (ret == 0)
			ret=BIO_ctrl(b->next_bio,cmd,num,ptr);
		break;
	case BIO_C_SET_BUFF_READ_DATA:
		if (num > ctx->ibuf_size)
			{
			p1=Malloc((int)num);
			if (p1 == NULL) goto malloc_error;
			if (ctx->ibuf != NULL) Free(ctx->ibuf);
			ctx->ibuf=p1;
			}
		ctx->ibuf_off=0;
		ctx->ibuf_len=(int)num;
		memcpy(ctx->ibuf,ptr,(int)num);
		ret=1;
		break;
	case BIO_C_SET_BUFF_SIZE:
		if (ptr != NULL)
			{
			ip=(int *)ptr;
			if (*ip == 0)
				{
				ibs=(int)num;
				obs=ctx->obuf_size;
				}
			else /* if (*ip == 1) */
				{
				ibs=ctx->ibuf_size;
				obs=(int)num;
				}
			}
		else
			{
			ibs=(int)num;
			obs=(int)num;
			}
		p1=ctx->ibuf;
		p2=ctx->obuf;
		if ((ibs > DEFAULT_BUFFER_SIZE) && (ibs != ctx->ibuf_size))
			{
			p1=(char *)Malloc((int)num);
			if (p1 == NULL) goto malloc_error;
			}
		if ((obs > DEFAULT_BUFFER_SIZE) && (obs != ctx->obuf_size))
			{
			p2=(char *)Malloc((int)num);
			if (p2 == NULL)
				{
				if (p1 != ctx->ibuf) Free(p1);
				goto malloc_error;
				}
			}
		if (ctx->ibuf != p1)
			{
			Free(ctx->ibuf);
			ctx->ibuf=p1;
			ctx->ibuf_off=0;
			ctx->ibuf_len=0;
			ctx->ibuf_size=ibs;
			}
		if (ctx->obuf != p2)
			{
			Free(ctx->obuf);
			ctx->obuf=p2;
			ctx->obuf_off=0;
			ctx->obuf_len=0;
			ctx->obuf_size=obs;
			}
		break;
	case BIO_C_DO_STATE_MACHINE:
		BIO_clear_retry_flags(b);
		ret=BIO_ctrl(b->next_bio,cmd,num,ptr);
		BIO_copy_next_retry(b);
		break;

	case BIO_CTRL_FLUSH:
		if (ctx->obuf_len <= 0)
			{
			ret=BIO_ctrl(b->next_bio,cmd,num,ptr);
			break;
			}

		for (;;)
			{
			BIO_clear_retry_flags(b);
			if (ctx->obuf_len > ctx->obuf_off)
				{
				r=BIO_write(b->next_bio,
					&(ctx->obuf[ctx->obuf_off]),
					ctx->obuf_len-ctx->obuf_off);
#if 0
fprintf(stderr,"FLUSH [%3d] %3d -> %3d\n",ctx->obuf_off,ctx->obuf_len-ctx->obuf_off,r);
#endif
				BIO_copy_next_retry(b);
				if (r <= 0) return((long)r);
				ctx->obuf_off+=r;
				}
			else
				{
				ctx->obuf_len=0;
				ctx->obuf_off=0;
				ret=1;
				break;
				}
			}
		break;
	case BIO_CTRL_DUP:
		dbio=(BIO *)ptr;
		if (	!BIO_set_read_buffer_size(dbio,ctx->ibuf_size) ||
			!BIO_set_write_buffer_size(dbio,ctx->obuf_size))
			ret=0;
		break;
	default:
		ret=BIO_ctrl(b->next_bio,cmd,num,ptr);
		break;
		}
	return(ret);
malloc_error:
	BIOerr(BIO_F_BUFFER_CTRL,ERR_R_MALLOC_FAILURE);
	return(0);
	}

static int buffer_gets(b,buf,size)
BIO *b;
char *buf;
int size;
	{
	BIO_F_BUFFER_CTX *ctx;
	int num=0,i,flag;
	char *p;

	ctx=(BIO_F_BUFFER_CTX *)b->ptr;
	size--; /* reserve space for a '\0' */
	BIO_clear_retry_flags(b);

	for (;;)
		{
		if (ctx->ibuf_len > 0)
			{
			p= &(ctx->ibuf[ctx->ibuf_off]);
			flag=0;
			for (i=0; (i<ctx->ibuf_len) && (i<size); i++)
				{
				*(buf++)=p[i];
				if (p[i] == '\n')
					{
					flag=1;
					i++;
					break;
					}
				}
			num+=i;
			size-=i;
			ctx->ibuf_len-=i;
			ctx->ibuf_off+=i;
			if ((flag) || (i == size))
				{
				*buf='\0';
				return(num);
				}
			}
		else	/* read another chunk */
			{
			i=BIO_read(b->next_bio,ctx->ibuf,ctx->ibuf_size);
			if (i <= 0)
				{
				BIO_copy_next_retry(b);
				if (i < 0) return((num > 0)?num:i);
				if (i == 0) return(num);
				}
			ctx->ibuf_len=i;
			ctx->ibuf_off=0;
			}
		}
	}

static int buffer_puts(b,str)
BIO *b;
char *str;
	{
	return(BIO_write(b,str,strlen(str)));
	}

#endif
