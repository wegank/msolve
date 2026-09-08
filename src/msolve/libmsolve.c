/* This file is part of msolve.
 *
 * msolve is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * msolve is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with msolve.  If not, see <https://www.gnu.org/licenses/>
 *
 * Authors:
 * Jérémy Berthomieu
 * Christian Eder
 * Mohab Safey El Din */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "msolve-data.h"
#include "msolve-data.c"
#include "streams.h"

/* iofiles.c, hilbert.c, primes.c and msolve.c are included below as a single
 * translation unit and some of them call into functions defined further
 * down that list (hilbert.c uses iofiles.c's display_monomial_full;
 * msolve.c/lifting-gb.c, included via msolve.c, use primes.c's
 * next_prime). Forward-declaring them here makes the #include order below
 * cosmetic instead of load-bearing. */
static inline int32_t display_monomial_full(FILE *file, const int nv,
                                            char **vnames,
                                            int64_t pos, int32_t *bexp);
uint32_t next_prime(uint32_t n);

#include "iofiles.c"
#include "hilbert.c"
#include "primes.c"
#include "../crt/mpz_CRT_ui.c"
#include "../crt/mpq_reconstruct.c"
#include "../usolve/data_usolve.c"
#include "../usolve/libusolve.h"
#include "../neogb/libneogb.h"
#include "msolve.c"

