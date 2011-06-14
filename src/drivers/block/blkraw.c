/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/drivers/block/blkraw.c                                   *
 * Created:     2004-09-17 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2004-2011 Hampa Hug <hampa@hampa.ch>                     *
 *****************************************************************************/

/*****************************************************************************
 * This program is free software. You can redistribute it and / or modify it *
 * under the terms of the GNU General Public License version 2 as  published *
 * by the Free Software Foundation.                                          *
 *                                                                           *
 * This program is distributed in the hope  that  it  will  be  useful,  but *
 * WITHOUT  ANY   WARRANTY,   without   even   the   implied   warranty   of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU  General *
 * Public License for more details.                                          *
 *****************************************************************************/


#include "blkraw.h"

#include <stdlib.h>


static
int dsk_img_read (disk_t *dsk, void *buf, uint32_t i, uint32_t n)
{
	disk_img_t *img;
	uint64_t   ofs, cnt;

	if ((i + n) > dsk->blocks) {
		return (1);
	}

	img = dsk->ext;

	ofs = img->start + 512 * (uint64_t) i;
	cnt = 512 * (uint64_t) n;

	if (dsk_read (img->fp, buf, ofs, cnt)) {
		return (1);
	}

	return (0);
}

static
int dsk_img_write (disk_t *dsk, const void *buf, uint32_t i, uint32_t n)
{
	disk_img_t *img;
	uint64_t   ofs, cnt;

	if (dsk->readonly) {
		return (1);
	}

	if ((i + n) > dsk->blocks) {
		return (1);
	}

	img = dsk->ext;

	ofs = img->start + 512 * (uint64_t) i;
	cnt = 512 * (uint64_t) n;

	if (dsk_write (img->fp, buf, ofs, cnt)) {
		return (1);
	}

	fflush (img->fp);

	return (0);
}

static
void dsk_img_del (disk_t *dsk)
{
	disk_img_t *img;

	img = dsk->ext;

	fclose (img->fp);
	free (img);
}

disk_t *dsk_img_open_fp (FILE *fp, uint32_t n, uint32_t c, uint32_t h, uint32_t s,
	uint64_t ofs, int ro)
{
	disk_img_t *img;

	img = malloc (sizeof (disk_img_t));
	if (img == NULL) {
		return (NULL);
	}

	dsk_init (&img->dsk, img, n, c, h, s);

	dsk_set_type (&img->dsk, PCE_DISK_RAW);

	dsk_set_readonly (&img->dsk, ro);

	img->dsk.del = dsk_img_del;
	img->dsk.read = dsk_img_read;
	img->dsk.write = dsk_img_write;

	img->start = ofs;

	img->fp = fp;

	return (&img->dsk);
}

disk_t *dsk_img_open (const char *fname, uint32_t n, uint32_t c, uint32_t h, uint32_t s,
	uint64_t ofs, int ro)
{
	disk_t *dsk;
	FILE   *fp;

	if (ro) {
		fp = fopen (fname, "rb");
	}
	else {
		fp = fopen (fname, "r+b");
	}

	if (fp == NULL) {
		return (NULL);
	}

	dsk = dsk_img_open_fp (fp, n, c, h, s, ofs, ro);

	if (dsk == NULL) {
		fclose (fp);
		return (NULL);
	}

	dsk_set_fname (dsk, fname);

	return (dsk);
}

disk_t *dsk_dosimg_open_fp (FILE *fp, uint64_t ofs, int ro)
{
	unsigned char buf[512];
	uint32_t      c, h, s, n;
	disk_t        *dsk;

	if (dsk_read (fp, buf, ofs, 512)) {
		return (NULL);
	}

	/* boot sector id */
	if ((buf[510] != 0x55) || (buf[511] != 0xaa)) {
		return (NULL);
	}

	/* sector size */
	if (dsk_get_uint16_le (buf, 11) != 512) {
		return (NULL);
	}

	/* total sectors */
	n = dsk_get_uint16_le (buf, 19);
	if (n == 0) {
		n = dsk_get_uint32_le (buf, 32);
	}

	h = dsk_get_uint16_le (buf, 26);
	s = dsk_get_uint16_le (buf, 24);
	c = n / (h * s);

	dsk = dsk_img_open_fp (fp, n, c, h, s, ofs, ro);

	return (dsk);
}

disk_t *dsk_dosimg_open (const char *fname, uint64_t ofs, int ro)
{
	disk_t *dsk;
	FILE   *fp;

	if (ro) {
		fp = fopen (fname, "rb");
	}
	else {
		fp = fopen (fname, "r+b");
	}

	if (fp == NULL) {
		return (NULL);
	}

	dsk = dsk_dosimg_open_fp (fp, ofs, ro);

	if (dsk == NULL) {
		fclose (fp);
		return (NULL);
	}

	dsk_set_fname (dsk, fname);

	return (dsk);
}

disk_t *dsk_mbrimg_open_fp (FILE *fp, uint64_t ofs, int ro)
{
	unsigned      i;
	unsigned char *p;
	unsigned char buf[512];
	uint32_t      c, h, s, n;
	uint32_t      tc1, th1, ts1, tn;
	uint32_t      tc2, th2, ts2;
	disk_t        *dsk;

	if (dsk_read (fp, buf, ofs, 512)) {
		return (NULL);
	}

	/* mbr id */
	if ((buf[510] != 0x55) || (buf[511] != 0xaa)) {
		return (NULL);
	}

	c = 0;
	h = 0;
	s = 0;
	n = 0;

	for (i = 0; i < 4; i++) {
		p = buf + 0x1be + 16 * i;

		if (p[0] & 0x7f) {
			return (NULL);
		}

		/* partition end, in blocks */
		tn = dsk_get_uint32_le (p, 8);
		tn += dsk_get_uint32_le (p, 12);
		n = (tn > n) ? tn : n;

		/* partition start */
		tc1 = p[3] | ((p[2] & 0xc0) << 2);
		th1 = p[1];
		ts1 = p[2] & 0x3f;
		h = (th1 > h) ? th1 : h;
		s = (ts1 > s) ? ts1 : s;

		/* partition end */
		tc2 = p[7] | ((p[6] & 0xc0) << 2);
		th2 = p[5];
		ts2 = p[6] & 0x3f;
		h = (th2 > h) ? th2 : h;
		s = (ts2 > s) ? ts2 : s;

		/* check if start is before end */
		if (tc2 < tc1) {
			return (NULL);
		}
		else if (tc2 == tc1) {
			if (th2 < th1) {
				return (NULL);
			}
			else if (th2 == th1) {
				if (ts2 < ts1) {
					return (NULL);
				}
			}
		}
	}

	if (s == 0) {
		return (NULL);
	}

	h = h + 1;
	c = n / (h * s);

	if (c == 0) {
		return (NULL);
	}

	dsk = dsk_img_open_fp (fp, n, c, h, s, ofs, ro);

	return (dsk);
}

disk_t *dsk_mbrimg_open (const char *fname, uint64_t ofs, int ro)
{
	disk_t *dsk;
	FILE   *fp;

	if (ro) {
		fp = fopen (fname, "rb");
	}
	else {
		fp = fopen (fname, "r+b");
	}

	if (fp == NULL) {
		return (NULL);
	}

	dsk = dsk_mbrimg_open_fp (fp, ofs, ro);

	if (dsk == NULL) {
		fclose (fp);
		return (NULL);
	}

	dsk_set_fname (dsk, fname);

	return (dsk);
}

/* HFS image */
disk_t *dsk_hfsimg_open_fp (FILE *fp, uint64_t ofs, int ro)
{
	uint64_t      cnt;
	uint32_t      n;
	disk_t        *dsk;
	unsigned char buf[512];

	if (dsk_read (fp, buf, ofs + 1024, 512)) {
		return (NULL);
	}

	if ((buf[0] != 'B') || (buf[1] != 'D')) {
		return (NULL);
	}

	if (dsk_get_filesize (fp, &cnt)) {
		return (NULL);
	}

	n = (cnt - ofs) / 512;

	dsk = dsk_img_open_fp (fp, n, 0, 0, 0, ofs, ro);

	dsk->blocks = n;

	return (dsk);
}

disk_t *dsk_hfsimg_open (const char *fname, uint64_t ofs, int ro)
{
	disk_t *dsk;
	FILE   *fp;

	if (ro) {
		fp = fopen (fname, "rb");
	}
	else {
		fp = fopen (fname, "r+b");
	}

	if (fp == NULL) {
		return (NULL);
	}

	dsk = dsk_hfsimg_open_fp (fp, ofs, ro);

	if (dsk == NULL) {
		fclose (fp);
		return (NULL);
	}

	dsk_set_fname (dsk, fname);

	return (dsk);
}

/* Macintosh harddisk image */
disk_t *dsk_macimg_open_fp (FILE *fp, uint64_t ofs, int ro)
{
	uint32_t      n;
	disk_t        *dsk;
	unsigned char buf[512];

	if (dsk_read (fp, buf, ofs, 512)) {
		return (NULL);
	}

	if ((buf[0] != 'E') || (buf[1] != 'R')) {
		return (NULL);
	}

	n = dsk_get_uint32_be (buf, 4);

	dsk = dsk_img_open_fp (fp, n, 0, 0, 0, ofs, ro);

	return (dsk);
}

disk_t *dsk_macimg_open (const char *fname, uint64_t ofs, int ro)
{
	disk_t *dsk;
	FILE   *fp;

	fp = fopen (fname, ro ? "rb" : "r+b");
	if (fp == NULL) {
		return (NULL);
	}

	dsk = dsk_macimg_open_fp (fp, ofs, ro);

	if (dsk == NULL) {
		fclose (fp);
		return (NULL);
	}

	dsk_set_fname (dsk, fname);

	return (dsk);
}

disk_t *dsk_fdimg_open_fp (FILE *fp, uint64_t ofs, int ro)
{
	uint64_t cnt;

	if (dsk_get_filesize (fp, &cnt)) {
		return (NULL);
	}

	if (cnt <= ofs) {
		return (NULL);
	}

	switch (cnt - ofs) {
	case 160UL * 1024UL:
		return (dsk_img_open_fp (fp, 0, 40, 1, 8, ofs, ro));

	case 180UL * 1024UL:
		return (dsk_img_open_fp (fp, 0, 40, 1, 9, ofs, ro));

	case 320UL * 1024UL:
		return (dsk_img_open_fp (fp, 0, 40, 2, 8, ofs, ro));

	case 400UL * 1024UL:
		return (dsk_img_open_fp (fp, 0, 40, 2, 10, ofs, ro));

	case 360UL * 1024UL:
		return (dsk_img_open_fp (fp, 0, 40, 2, 9, ofs, ro));

	case 720UL * 1024UL:
		return (dsk_img_open_fp (fp, 0, 80, 2, 9, ofs, ro));

	case 800UL * 1024UL:
		return (dsk_img_open_fp (fp, 0, 80, 2, 10, ofs, ro));

	case 1200UL * 1024UL:
		return (dsk_img_open_fp (fp, 0, 80, 2, 15, ofs, ro));

	case 1440UL * 1024UL:
		return (dsk_img_open_fp (fp, 0, 80, 2, 18, ofs, ro));

	case 2880UL * 1024UL:
		return (dsk_img_open_fp (fp, 0, 80, 2, 36, ofs, ro));
	}

	return (NULL);
}

disk_t *dsk_fdimg_open (const char *fname, uint64_t ofs, int ro)
{
	disk_t *dsk;
	FILE   *fp;

	if (ro) {
		fp = fopen (fname, "rb");
	}
	else {
		fp = fopen (fname, "r+b");
	}

	if (fp == NULL) {
		return (NULL);
	}

	dsk = dsk_fdimg_open_fp (fp, ofs, ro);

	if (dsk == NULL) {
		fclose (fp);
		return (NULL);
	}

	dsk_set_fname (dsk, fname);

	return (dsk);
}

disk_t *dsk_autoimg_open_fp (FILE *fp, uint64_t ofs, int ro)
{
	unsigned      i;
	uint64_t      cnt;
	uint32_t      n;
	unsigned char buf[512];
	disk_t        *dsk;

	if (dsk_get_filesize (fp, &cnt)) {
		return (NULL);
	}

	if (cnt <= ofs) {
		return (NULL);
	}

	cnt -= ofs;

	if ((cnt < 512) || (cnt & 511)) {
		return (NULL);
	}

	if (dsk_read (fp, buf, ofs, 512)) {
		return (NULL);
	}

	for (i = 0; i < 512; i++) {
		if (buf[i] != 0) {
			return (NULL);
		}
	}

	n = cnt / 512;

	dsk = dsk_img_open_fp (fp, n, 0, 0, 0, ofs, ro);

	return (dsk);
}

disk_t *dsk_autoimg_open (const char *fname, uint64_t ofs, int ro)
{
	disk_t *dsk;
	FILE   *fp;

	if (ro) {
		fp = fopen (fname, "rb");
	}
	else {
		fp = fopen (fname, "r+b");
	}

	if (fp == NULL) {
		return (NULL);
	}

	dsk = dsk_autoimg_open_fp (fp, ofs, ro);

	if (dsk == NULL) {
		fclose (fp);
		return (NULL);
	}

	dsk_set_fname (dsk, fname);

	return (dsk);
}

void dsk_img_set_offset (disk_t *dsk, uint64_t ofs)
{
	disk_img_t *img;

	img = dsk->ext;

	img->start = ofs;
}

int dsk_img_create_fp (FILE *fp, uint32_t n, uint32_t c, uint32_t h, uint32_t s,
	uint64_t ofs)
{
	uint64_t      cnt;
	unsigned char buf;

	if (dsk_adjust_chs (&n, &c, &h, &s)) {
		return (1);
	}

	cnt = 512 * (uint64_t) n;

	if (cnt == 0) {
		return (1);
	}

	buf = 0;

	if (dsk_write (fp, &buf, ofs + cnt - 1, 1)) {
		return (1);
	}

	return (0);
}

int dsk_img_create (const char *fname, uint32_t n, uint32_t c, uint32_t h, uint32_t s, uint64_t ofs)
{
	int  r;
	FILE *fp;

	fp = fopen (fname, "wb");
	if (fp == NULL) {
		return (1);
	}

	r = dsk_img_create_fp (fp, n, c, h, s, ofs);

	fclose (fp);

	return (r);
}