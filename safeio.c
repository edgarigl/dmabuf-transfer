/*
 * Safe IO, take care of signal interruption and partial reads/writes.
 *
 * Copyright (c) 2011 Edgar E. Iglesias.
 * Written by Edgar E. Iglesias.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define _LARGEFILE64_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define D(x)

ssize_t safe_read(int fd, void *rbuf, size_t count)
{
	ssize_t r;
	size_t rlen = 0;
	unsigned char *buf = rbuf;

	do {
		if ((r = read(fd, buf + rlen, count - rlen)) < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		rlen += r;
	} while (rlen < count && r);

	return rlen;
}

ssize_t safe_write(int fd, const void *wbuf, size_t count)
{
	ssize_t r;
	size_t wlen = 0;
	const unsigned char *buf = wbuf;

	do {
		if ((r = write(fd, buf + wlen, count - wlen)) < 0) {
			if (errno == EINTR) {
				continue;
			} else if (errno == EAGAIN) 
				break;
			return -1;
		}

		wlen += r;
	} while (wlen < count);

	return wlen;
}
