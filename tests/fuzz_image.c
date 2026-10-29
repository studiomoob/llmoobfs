/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * libFuzzer target for the image parser moob_parse() of common/format.c, the code the M1 module
 * (kernel/format.c includes it) runs at mount time and fsck.moobfs runs offline over an
 * untrusted image (security roadmap S7, journal 0237). Build with
 *   clang -fsanitize=fuzzer,address,undefined -Iinclude tests/fuzz_image.c common/format.c common/hash.c
 * Byte 0 holds flags; the rest is the image, zero-padded to a multiple of MOOB_BLOCK. With bit 0
 * set the target first writes the payload SHA-256s, the two table CRCs and the superblock CRC
 * that the image's own fields call for, so that mutations reach the checks behind the
 * checksums; with bit 0 clear the bytes are parsed as given. The reader fails a read outside the
 * image, as a short device would.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "moobfs_format.h"

#define MAX_BLOCKS 6U

struct dev { const m_u8 *p; m_u64 size; };

static int dev_read(void *opaque, m_u64 off, void *out, size_t len)
{
	const struct dev *d = opaque;

	if (off > d->size || len > d->size - off)
		return -1;
	memcpy(out, d->p + off, len);
	return 0;
}

/* writes the checksums the image's fields ask for, where they fall inside the image */
static void fix_checksums(m_u8 *img, m_u64 size)
{
	m_u32 files, objects, i;
	m_u64 otab, ftab, obytes, fbytes;

	if (size < MOOB_BLOCK)
		return;
	files = moob_get32(img + 16);
	objects = moob_get32(img + 20);
	if (files > MOOB_MAX_FILES || objects > MOOB_MAX_FILES)
		return;
	otab = moob_get64(img + 24);
	ftab = moob_get64(img + 32);
	obytes = moob_align((m_u64)objects * MOOB_OBJECT_REC);
	fbytes = moob_align((m_u64)files * MOOB_FILE_REC);
	for (i = 0; i < objects; i++) {
		m_u64 rec = otab + (m_u64)i * MOOB_OBJECT_REC, off, len;
		struct moob_sha256 h;

		if (otab > size || rec > size - MOOB_OBJECT_REC)
			break;
		off = moob_get64(img + rec + 8);
		len = moob_get64(img + rec + 16);
		if (off > size || len > size - off)
			continue;
		moob_sha_init(&h);
		moob_sha_update(&h, img + off, (size_t)len);
		moob_sha_final(&h, img + rec + 24);
	}
	if (otab <= size && obytes <= size - otab)
		moob_put32(img + MOOB_OBJECT_CRC, ~moob_crc32c(~0U, img + otab, (size_t)obytes));
	if (ftab <= size && fbytes <= size - ftab)
		moob_put32(img + MOOB_FILE_CRC, ~moob_crc32c(~0U, img + ftab, (size_t)fbytes));
	moob_put32(img + MOOB_SUPER_CRC, 0);
	moob_put32(img + MOOB_SUPER_CRC, ~moob_crc32c(~0U, img, MOOB_BLOCK));
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	static struct moob_image im;
	m_u8 scratch[MOOB_BLOCK];
	const char *why = NULL;
	struct dev d;
	m_u8 *img;
	size_t len;

	if (!size || size - 1 > MAX_BLOCKS * MOOB_BLOCK)
		return 0;
	len = (size_t)moob_align(size - 1);
	img = calloc(1, len ? len : 1);
	if (!img)
		return 0;
	memcpy(img, data + 1, size - 1);
	if (data[0] & 1)
		fix_checksums(img, len);
	memset(&im, 0, sizeof im);
	d.p = img;
	d.size = len;
	if (!moob_parse(&im, len, dev_read, &d, scratch, &why)) {
		m_u32 i;

		/* an accepted image must keep the invariants fsck.moobfs and the module rely on */
		if (im.image_size != len || !im.file_count || im.file_count > MOOB_MAX_FILES ||
		    !im.object_count || im.object_count > im.file_count)
			abort();
		for (i = 0; i < im.file_count; i++)
			if (im.files[i].object_index >= im.object_count ||
			    strlen(im.files[i].name) > MOOB_NAME_MAX)
				abort();
	} else if (!why) {
		abort();
	}
	free(img);
	return 0;
}
