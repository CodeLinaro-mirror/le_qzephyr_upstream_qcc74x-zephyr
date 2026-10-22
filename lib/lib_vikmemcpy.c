/*
 * SPDX-License-Identifier: Zlib
 * SPDX-FileCopyrightText: Copyright (C) 1999-2010 Daniel Vik
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any
 * damages arising from the use of this software.
 * Permission is granted to anyone to use this software for any
 * purpose, including commercial applications, and to alter it and
 * redistribute it freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you
 *    must not claim that you wrote the original software. If you
 *    use this software in a product, an acknowledgment in the
 *    use this software in a product, an acknowledgment in the
 *    product documentation would be appreciated but is not
 *    required.
 *
 * 2. Altered source versions must be plainly marked as such, and
 *    must not be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 *    dist
 *
 *
 * This is a version of the optimized memcpy by Daniel Vik, adapted from
 * NuttX for the QCC74x Zephyr Wi-Fi throughput profile. This adaptation uses
 * bounded first/last-word handling so memcpy never reads outside src[0..n).
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "The QCC74x Vik memcpy adaptation requires little-endian byte order"
#endif

typedef uint32_t vik_word __attribute__((__may_alias__));

static inline void copy_bytes(uint8_t **dst, const uint8_t **src, size_t count)
{
	while (count--) {
		*(*dst)++ = *(*src)++;
	}
}

void *memcpy(void *dest, const void *src, size_t count)
{
	uint8_t *dst8 = dest;
	const uint8_t *src8 = src;
	void *result = dest;

	/* Word stores require an aligned destination. */
	while (count != 0U && ((uintptr_t)dst8 & 3U) != 0U) {
		*dst8++ = *src8++;
		count--;
	}

	if (((uintptr_t)src8 & 3U) == 0U) {
		vik_word *dst32 = (vik_word *)(uintptr_t)dst8;
		const vik_word *src32 = (const vik_word *)(uintptr_t)src8;

		while (count >= 32U) {
			dst32[0] = src32[0];
			dst32[1] = src32[1];
			dst32[2] = src32[2];
			dst32[3] = src32[3];
			dst32[4] = src32[4];
			dst32[5] = src32[5];
			dst32[6] = src32[6];
			dst32[7] = src32[7];
			dst32 += 8;
			src32 += 8;
			count -= 32U;
		}

		while (count >= 4U) {
			*dst32++ = *src32++;
			count -= 4U;
		}

		dst8 = (uint8_t *)(uintptr_t)dst32;
		src8 = (const uint8_t *)(uintptr_t)src32;
	} else if (count >= 4U) {
		unsigned int shift = ((uintptr_t)src8 & 3U) * 8U;
		unsigned int inverse_shift = 32U - shift;
		vik_word *dst32 = (vik_word *)(uintptr_t)dst8;
		const uint8_t *aligned_src;
		vik_word previous;

		/* Copy one word with bounded byte reads. Afterwards, aligning src8
		 * down remains within the original source object. */
		vik_word first = (vik_word)src8[0] |
				 ((vik_word)src8[1] << 8) |
				 ((vik_word)src8[2] << 16) |
				 ((vik_word)src8[3] << 24);

		*dst32++ = first;
		dst8 += 4;
		src8 += 4;
		count -= 4U;

		aligned_src = (const uint8_t *)((uintptr_t)src8 & ~(uintptr_t)3U);
		previous = *(const vik_word *)(uintptr_t)aligned_src;

		/* Unroll the common bulk path as in the original Vik implementation. */
		while (count >= 36U) {
			vik_word next;

			next = *(const vik_word *)(uintptr_t)(aligned_src + 4);
			dst32[0] = (previous >> shift) | (next << inverse_shift);
			previous = next;
			next = *(const vik_word *)(uintptr_t)(aligned_src + 8);
			dst32[1] = (previous >> shift) | (next << inverse_shift);
			previous = next;
			next = *(const vik_word *)(uintptr_t)(aligned_src + 12);
			dst32[2] = (previous >> shift) | (next << inverse_shift);
			previous = next;
			next = *(const vik_word *)(uintptr_t)(aligned_src + 16);
			dst32[3] = (previous >> shift) | (next << inverse_shift);
			previous = next;
			next = *(const vik_word *)(uintptr_t)(aligned_src + 20);
			dst32[4] = (previous >> shift) | (next << inverse_shift);
			previous = next;
			next = *(const vik_word *)(uintptr_t)(aligned_src + 24);
			dst32[5] = (previous >> shift) | (next << inverse_shift);
			previous = next;
			next = *(const vik_word *)(uintptr_t)(aligned_src + 28);
			dst32[6] = (previous >> shift) | (next << inverse_shift);
			previous = next;
			next = *(const vik_word *)(uintptr_t)(aligned_src + 32);
			dst32[7] = (previous >> shift) | (next << inverse_shift);
			previous = next;

			aligned_src += 32;
			dst32 += 8;
			dst8 += 32;
			src8 += 32;
			count -= 32U;
		}

		/* Leave the last 4-7 bytes to the bounded tail. Loading the next
		 * aligned word is then always inside the remaining source range. */
		while (count >= 8U) {
			vik_word next = *(const vik_word *)(uintptr_t)(aligned_src + 4);

			*dst32++ = (previous >> shift) | (next << inverse_shift);
			previous = next;
			aligned_src += 4;
			dst8 += 4;
			src8 += 4;
			count -= 4U;
		}
	}

	copy_bytes(&dst8, &src8, count);
	return result;
}
