/*
 * Copyright (c) 2011 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Venkatesh Srinivas <me@endeavour.zapto.org>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <hiten@uk.FreeBSD.ORG> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you
 * think this stuff is worth it, you can buy me a beer in return. Hiten Pandya.
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/dev/md/md.c,v 1.8.2.2 2002/08/19 17:43:34 jdp Exp $
 */

/*
 * fmemopen -- Open a memory buffer stream
 *
 * POSIX 1003.1-2008
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>

static int __fmemopen_closefn (void *);
static int __fmemopen_readfn(void *, char *, int);
static fpos_t __fmemopen_seekfn (void *, fpos_t, int);
static int __fmemopen_writefn(void *, const char *, int);

struct fmemopen_cookie {
	char *buffer;
	int mybuffer;
	size_t size;
	size_t pos;
	size_t maxpos;
};

static int
__fmemopen_readfn(void *cookie, char *buf, int len)
{
	struct fmemopen_cookie *c;

	c = (struct fmemopen_cookie *) cookie;
	if (c == NULL) {
		errno = EBADF;
		return (-1);
	}

	if ((c->pos + len) > c->size) {
		if (c->pos == c->size)
			return -1;
		len = c->size - c->pos;
	}

	memcpy(buf, &(c->buffer[c->pos]), len);

	c->pos += len;

	if (c->pos > c->maxpos)
		c->maxpos = c->pos;

	return (len);
}

static int
__fmemopen_writefn (void *cookie, const char *buf, int len)
{
	struct fmemopen_cookie *c;
	int addnullc;

	c = (struct fmemopen_cookie *) cookie;
	if (c == NULL) {
		errno = EBADF;
		return (-1);
	}

	addnullc = ((len == 0) || (buf[len - 1] != '\0')) ? 1 : 0;

	if ((c->pos + len + addnullc) > c->size) {
		if ((c->pos + addnullc) == c->size)
			return -1;
		len = c->size - c->pos - addnullc;
	}

	memcpy(&(c->buffer[c->pos]), buf, len);

	c->pos += len;
	if (c->pos > c->maxpos) {
		c->maxpos = c->pos;
		if (addnullc)
			c->buffer[c->maxpos] = '\0';
	}

	return (len);
}

static fpos_t
__fmemopen_seekfn(void *cookie, fpos_t pos, int whence)
{
	//fpos_t np =  0;
	fpos_t np =  (fpos_t)0;
	struct fmemopen_cookie *c;

	c = (struct fmemopen_cookie *) cookie;

	switch(whence) {
	case (SEEK_SET):
		np = pos;
		break;
	case (SEEK_CUR):
		np = c->pos + pos;
		break;
	case (SEEK_END):
		np = c->size - pos;
		break;
	}

	if ((np < 0) || (np > c->size))
		return (-1);

	c->pos = np;

	return (np);
}

static int
__fmemopen_closefn (void *cookie)
{
	struct fmemopen_cookie *c;

	c = (struct fmemopen_cookie*) cookie;

	if (c->mybuffer)
		free(c->buffer);
	free(c);

	return (0);
}

FILE *
fmemopen(void *restrict buffer, size_t s, const char *restrict mode)
{
	FILE *f = NULL;
	struct fmemopen_cookie *c;

	c = malloc(sizeof (struct fmemopen_cookie));
	if (c == NULL)
		return NULL;

	c->mybuffer = (buffer == NULL);

	if (c->mybuffer) {
		c->buffer = malloc(s);
		if (c->buffer == NULL) {
			free(c);
			return NULL;
		}
		c->buffer[0] = '\0';
	} else {
		c->buffer = buffer;
	}
	c->size = s;
	if (mode[0] == 'w')
		c->buffer[0] = '\0';
	c->maxpos = strlen(c->buffer);

	if (mode[0] == 'a')
		c->pos = c->maxpos;
	else
		c->pos = 0;

	f = funopen(c,
		    __fmemopen_readfn, /* string stream read */
		    __fmemopen_writefn, /* string stream write */
		    __fmemopen_seekfn, /* string stream seek */
		    __fmemopen_closefn /* string stream close */
		    );

	if (f == NULL)
		free(c);

	return (f);
}
