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

#include "data.h"

#ifdef HAVE_AVX2
#include <immintrin.h>
#endif

static inline cf32_t *normalize_dense_matrix_row_ff_32(
        cf32_t *row,
        const hm_t len,
        const uint32_t fc
        )
{
    len_t i;

    const hm_t os       = len % UNROLL;
    const uint32_t inv  = mod_p_inverse_32(row[0], fc);

    for (i = 1; i < os; ++i) {
        row[i]  = (cf32_t)(((uint64_t)row[i] * inv) % fc);
    }
    /* we need to set i to os since os < 1 is possible */
    for (i = os; i < len; i += UNROLL) {
        row[i]    = (cf32_t)(((uint64_t)row[i] * inv) % fc);
        row[i+1]  = (cf32_t)(((uint64_t)row[i+1] * inv) % fc);
        row[i+2]  = (cf32_t)(((uint64_t)row[i+2] * inv) % fc);
        row[i+3]  = (cf32_t)(((uint64_t)row[i+3] * inv) % fc);
    }
    row[0]  = 1;

    return row;
}

static inline cf32_t *normalize_sparse_matrix_row_ff_32(
        cf32_t *row,
        const len_t os,
        const len_t len,
        const uint32_t fc
        )
{
    len_t i;

    const uint32_t inv  = mod_p_inverse_32(row[0], fc);

    for (i = 0; i < os; ++i) {
        row[i]  =  (cf32_t)(((uint64_t)row[i] * inv) % fc);
    }
    /* we need to set i to os since os < 1 is possible */
    for (i = os; i < len; i += UNROLL) {
        row[i]    = (cf32_t)(((uint64_t)row[i] * inv) % fc);
        row[i+1]  = (cf32_t)(((uint64_t)row[i+1] * inv) % fc);
        row[i+2]  = (cf32_t)(((uint64_t)row[i+2] * inv) % fc);
        row[i+3]  = (cf32_t)(((uint64_t)row[i+3] * inv) % fc);
    }
    row[0]  = 1;

    return row;
}

static inline cf32_t *multiply_sparse_matrix_row_ff_32(
        cf32_t *row,
        const cf32_t mul,
        const len_t os,
        const len_t len,
        const uint32_t fc
        )
{
    len_t i;


    for (i = 0; i < os; ++i) {
        row[i]  =  (cf32_t)(((uint64_t)row[i] * mul) % fc);
    }
    /* we need to set i to os since os < 1 is possible */
    for (i = os; i < len; i += UNROLL) {
        row[i]    = (cf32_t)(((uint64_t)row[i] * mul) % fc);
        row[i+1]  = (cf32_t)(((uint64_t)row[i+1] * mul) % fc);
        row[i+2]  = (cf32_t)(((uint64_t)row[i+2] * mul) % fc);
        row[i+3]  = (cf32_t)(((uint64_t)row[i+3] * mul) % fc);
    }

    return row;
}

static hm_t *reduce_dense_row_by_known_pivots_sparse_17_bit(
        int64_t *dr,
        mat_t *mat,
        const bs_t * const bs,
        hm_t * const * const pivs,
        const hi_t dpiv,    /* pivot of dense row at the beginning */
        const hm_t tmp_pos, /* position of new coeffs array in tmpcf */
        stat_t *st
        )
{
    hi_t i, j, k;
    hm_t *dts;
    cf32_t *cfs;
    int64_t np  = -1;
    const int64_t mod           = (int64_t)st->fc;
    const len_t ncols           = mat->nc;
    const len_t ncl             = mat->ncl;
    cf32_t * const * const mcf  = mat->cf_32;

#ifdef HAVE_AVX2
    int64_t res[4];
    __m256i redv, mulv, prodv, drv, resv;
#endif

    k = 0;
    for (i = dpiv; i < ncols; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        if (dr[i] == 0) {
            continue;
        }
        if (pivs[i] == NULL) {
            if (np == -1) {
                np  = i;
            }
            k++;
            continue;
        }

        /* found reducer row, get multiplier */
        const int64_t mul = mod - dr[i];
        dts   = pivs[i];
        if (i < ncl) {
            cfs   = bs->cf_32[dts[COEFFS]];
        } else {
            cfs   = mcf[dts[COEFFS]];
        }
#ifdef HAVE_AVX2
        const len_t len = dts[LENGTH];
        const len_t os  = len % 8;
        const hm_t * const ds  = dts + OFFSET;
        const uint32_t mul32 = (int32_t)(mod - dr[i]);
        mulv  = _mm256_set1_epi32(mul32);
        for (j = 0; j < os; ++j) {
            dr[ds[j]]  +=  mul * cfs[j];
        }
        for (; j < len; j += 8) {
            redv  = _mm256_lddqu_si256((__m256i*)(cfs+j));
            drv   = _mm256_setr_epi64x(
                dr[ds[j+1]],
                dr[ds[j+3]],
                dr[ds[j+5]],
                dr[ds[j+7]]);
            /* first four mult-adds -- lower */
            prodv = _mm256_mul_epu32(mulv, _mm256_srli_epi64(redv, 32));
            resv  = _mm256_add_epi64(drv, prodv);
            _mm256_storeu_si256((__m256i*)(res), resv);
            dr[ds[j+1]] = res[0];
            dr[ds[j+3]] = res[1];
            dr[ds[j+5]] = res[2];
            dr[ds[j+7]] = res[3];
            /* second four mult-adds -- higher */
            prodv = _mm256_mul_epu32(mulv, redv);
            drv   = _mm256_setr_epi64x(
                dr[ds[j]],
                dr[ds[j+2]],
                dr[ds[j+4]],
                dr[ds[j+6]]);
            resv  = _mm256_add_epi64(drv, prodv);
            _mm256_storeu_si256((__m256i*)(res), resv);
            dr[ds[j]]   = res[0];
            dr[ds[j+2]] = res[1];
            dr[ds[j+4]] = res[2];
            dr[ds[j+6]] = res[3];
        }
#else
        const len_t os  = dts[PRELOOP];
        const len_t len = dts[LENGTH];
        const hm_t * const ds  = dts + OFFSET;
        for (j = 0; j < os; ++j) {
            dr[ds[j]] +=  mul * cfs[j];
        }
        for (; j < len; j += UNROLL) {
            dr[ds[j]]   +=  mul * cfs[j];
            dr[ds[j+1]] +=  mul * cfs[j+1];
            dr[ds[j+2]] +=  mul * cfs[j+2];
            dr[ds[j+3]] +=  mul * cfs[j+3];
        }
#endif
        dr[i] = 0;
        st->application_nr_mult +=  len / 1000.0;
        st->application_nr_add  +=  len / 1000.0;
        st->application_nr_red++;
    }
    if (k == 0) {
        return NULL;
    }

    hm_t *row   = (hm_t *)malloc((unsigned long)(k+OFFSET) * sizeof(hm_t));
    cf32_t *cf  = (cf32_t *)malloc((unsigned long)(k) * sizeof(cf32_t));
    j = 0;
    hm_t *rs = row + OFFSET;
    for (i = ncl; i < ncols; ++i) {
        if (dr[i] != 0) {
            rs[j] = (hm_t)i;
            cf[j] = (cf32_t)dr[i];
            j++;
        }
    }
    row[COEFFS]   = tmp_pos;
    row[PRELOOP]  = j % UNROLL;
    row[LENGTH]   = j;
    mat->cf_32[tmp_pos]  = cf;

    return row;
}

static hm_t *trace_reduce_dense_row_by_known_pivots_sparse_17_bit(
        rba_t *rba,
        int64_t *dr,
        mat_t *mat,
        const bs_t * const bs,
        hm_t * const * const pivs,
        const hi_t dpiv,    /* pivot of dense row at the beginning */
        const hm_t tmp_pos, /* position of new coeffs array in tmpcf */
        const len_t mh,     /* multiplier hash for tracing */
        const len_t bi,     /* basis index of generating element */
        stat_t *st
        )
{
    hi_t i, j, k;
    hm_t *dts;
    cf32_t *cfs;
    int64_t np = -1;
    const int64_t mod           = (int64_t)st->fc;
    const len_t ncols           = mat->nc;
    const len_t ncl             = mat->ncl;
    cf32_t * const * const mcf  = mat->cf_32;

    k = 0;
    for (i = dpiv; i < ncols; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        if (dr[i] == 0) {
            continue;
        }
        if (pivs[i] == NULL) {
            if (np == -1) {
                np  = i;
            }
            k++;
            continue;
        }

        /* found reducer row, get multiplier */
        const int64_t mul = mod - dr[i];
        dts   = pivs[i];
        if (i < ncl) {
            cfs   = bs->cf_32[dts[COEFFS]];
            /* set corresponding bit of reducer in reducer bit array */
            rba[i/32] |= 1U << (i % 32);
        } else {
            cfs   = mcf[dts[COEFFS]];
        }
        const len_t os  = dts[PRELOOP];
        const len_t len = dts[LENGTH];
        const hm_t * const ds  = dts + OFFSET;
        for (j = 0; j < os; ++j) {
            dr[ds[j]] +=  mul * cfs[j];
        }
        for (; j < len; j += UNROLL) {
            dr[ds[j]]   +=  mul * cfs[j];
            dr[ds[j+1]] +=  mul * cfs[j+1];
            dr[ds[j+2]] +=  mul * cfs[j+2];
            dr[ds[j+3]] +=  mul * cfs[j+3];
        }
        dr[i] = 0;
        st->trace_nr_mult +=  len / 1000.0;
        st->trace_nr_add  +=  len / 1000.0;
        st->trace_nr_red++;
    }
    if (k == 0) {
        return NULL;
    }

    hm_t *row   = (hm_t *)malloc((unsigned long)(k+OFFSET) * sizeof(hm_t));
    cf32_t *cf  = (cf32_t *)malloc((unsigned long)(k) * sizeof(cf32_t));
    j = 0;
    hm_t *rs = row + OFFSET;
    for (i = ncl; i < ncols; ++i) {
        if (dr[i] != 0) {
            rs[j] = (hm_t)i;
            cf[j] = (cf32_t)dr[i];
            j++;
        }
    }
    row[BINDEX]   = bi;
    row[MULT]     = mh;
    row[COEFFS]   = tmp_pos;
    row[PRELOOP]  = j % UNROLL;
    row[LENGTH]   = j;
    mat->cf_32[tmp_pos]  = cf;

    return row;
}

static hm_t *reduce_dense_row_by_known_pivots_sparse_31_bit(
        int64_t *dr,
        mat_t *mat,
        const bs_t * const bs,
        hm_t *const *pivs,
        const hi_t dpiv,    /* pivot of dense row at the beginning */
        const hm_t tmp_pos, /* position of new coeffs array in tmpcf */
        stat_t *st
        )
{
    hi_t i, j, k;
    cf32_t *cfs;
    hm_t *dts;
    int64_t np = -1;
    const int64_t mod           = (int64_t)st->fc;
    const int64_t mod2          = (int64_t)st->fc * st->fc;
    const len_t ncols           = mat->nc;
    const len_t ncl             = mat->ncl;
    cf32_t * const * const mcf  = mat->cf_32;
#ifdef HAVE_AVX2
    int64_t res[4] __attribute__((aligned(32)));
    __m256i cmpv, redv, drv, mulv, prodv, resv, rresv;
    __m256i zerov= _mm256_set1_epi64x(0);
    __m256i mod2v = _mm256_set1_epi64x(mod2);
#endif

    k = 0;
    for (i = dpiv; i < ncols; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        if (dr[i] == 0) {
            continue;
        }
        if (pivs[i] == NULL) {
            if (np == -1) {
                np  = i;
            }
            k++;
            continue;
        }

        /* found reducer row, get multiplier */
        const int64_t mul = (int64_t)dr[i];
        dts   = pivs[i];
        if (i < ncl) {
            cfs   = bs->cf_32[dts[COEFFS]];
        } else {
            cfs   = mcf[dts[COEFFS]];
        }
#ifdef HAVE_AVX2
        const len_t len = dts[LENGTH];
        const len_t os  = len % 8;
        const hm_t * const ds  = dts + OFFSET;
        const uint32_t mul32 = (uint32_t)(dr[i]);
        mulv  = _mm256_set1_epi32(mul32);
        for (j = 0; j < os; ++j) {
            dr[ds[j]] -=  mul * cfs[j];
            dr[ds[j]] +=  (dr[ds[j]] >> 63) & mod2;
        }
        for (; j < len; j += 8) {
            redv  = _mm256_loadu_si256((__m256i*)(cfs+j));
            drv   = _mm256_setr_epi64x(
                dr[ds[j+1]],
                dr[ds[j+3]],
                dr[ds[j+5]],
                dr[ds[j+7]]);
            /* first four mult-adds -- lower */
            prodv = _mm256_mul_epu32(mulv, _mm256_srli_epi64(redv, 32));
            resv  = _mm256_sub_epi64(drv, prodv);
            cmpv  = _mm256_cmpgt_epi64(zerov, resv);
            rresv = _mm256_add_epi64(resv, _mm256_and_si256(cmpv, mod2v));
            _mm256_store_si256((__m256i*)(res), rresv);
            dr[ds[j+1]] = res[0];
            dr[ds[j+3]] = res[1];
            dr[ds[j+5]] = res[2];
            dr[ds[j+7]] = res[3];
            /* second four mult-adds -- higher */
            prodv = _mm256_mul_epu32(mulv, redv);
            drv   = _mm256_setr_epi64x(
                dr[ds[j]],
                dr[ds[j+2]],
                dr[ds[j+4]],
                dr[ds[j+6]]);
            resv  = _mm256_sub_epi64(drv, prodv);
            cmpv  = _mm256_cmpgt_epi64(zerov, resv);
            rresv = _mm256_add_epi64(resv, _mm256_and_si256(cmpv, mod2v));
            _mm256_store_si256((__m256i*)(res), rresv);
            dr[ds[j]]   = res[0];
            dr[ds[j+2]] = res[1];
            dr[ds[j+4]] = res[2];
            dr[ds[j+6]] = res[3];
        }
#else
        const len_t os  = dts[PRELOOP];
        const len_t len = dts[LENGTH];
        const hm_t * const ds = dts + OFFSET;
        for (j = 0; j < os; ++j) {
            dr[ds[j]]   -=  mul * cfs[j];
            dr[ds[j]]   +=  (dr[ds[j]] >> 63) & mod2;
        }
        for (; j < len; j += UNROLL) {
            dr[ds[j]]   -=  mul * cfs[j];
            dr[ds[j+1]] -=  mul * cfs[j+1];
            dr[ds[j+2]] -=  mul * cfs[j+2];
            dr[ds[j+3]] -=  mul * cfs[j+3];
            dr[ds[j]]   +=  (dr[ds[j]] >> 63) & mod2;
            dr[ds[j+1]] +=  (dr[ds[j+1]] >> 63) & mod2;
            dr[ds[j+2]] +=  (dr[ds[j+2]] >> 63) & mod2;
            dr[ds[j+3]] +=  (dr[ds[j+3]] >> 63) & mod2;
        }
#endif
        dr[i] = 0;
        st->application_nr_mult +=  len / 1000.0;
        st->application_nr_add  +=  len / 1000.0;
        st->application_nr_red++;
    }

    if (k == 0) {
        return NULL;
    }

    hm_t *row   = (hm_t *)malloc((unsigned long)(k+OFFSET) * sizeof(hm_t));
    cf32_t *cf  = (cf32_t *)malloc((unsigned long)(k) * sizeof(cf32_t));
    j = 0;
    hm_t *rs  = row + OFFSET;
    for (i = ncl; i < ncols; ++i) {
        if (dr[i] != 0) {
            rs[j] = (hm_t)i;
            cf[j] = (cf32_t)dr[i];
            j++;
        }
    }
    row[COEFFS]   = tmp_pos;
    row[PRELOOP]  = j % UNROLL;
    row[LENGTH]   = j;
    mat->cf_32[tmp_pos]  = cf;

    return row;
}

static hm_t *trace_reduce_dense_row_by_known_pivots_sparse_31_bit(
        rba_t *rba,
        int64_t *dr,
        mat_t *mat,
        const bs_t * const bs,
        hm_t *const *pivs,
        const hi_t dpiv,    /* pivot of dense row at the beginning */
        const hm_t tmp_pos, /* position of new coeffs array in tmpcf */
        const len_t mh,     /* multiplier hash for tracing */
        const len_t bi,     /* basis index of generating element */
        stat_t *st
        )
{
    hi_t i, j, k;
    cf32_t *cfs;
    hm_t *dts;
    int64_t np = -1;
    const int64_t mod           = (int64_t)st->fc;
    const int64_t mod2          = (int64_t)st->fc * st->fc;
    const len_t ncols           = mat->nc;
    const len_t ncl             = mat->ncl;
    cf32_t * const * const mcf  = mat->cf_32;
#ifdef HAVE_AVX2
    int64_t res[4] __attribute__((aligned(32)));
    __m256i cmpv, redv, drv, mulv, prodv, resv, rresv;
    __m256i zerov = _mm256_set1_epi64x(0);
    __m256i mod2v = _mm256_set1_epi64x(mod2);
#endif

    k = 0;
    for (i = dpiv; i < ncols; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        if (dr[i] == 0) {
            continue;
        }
        if (pivs[i] == NULL) {
            if (np == -1) {
                np  = i;
            }
            k++;
            continue;
        }

        /* found reducer row, get multiplier */
        const int64_t mul = (int64_t)dr[i];
        dts   = pivs[i];
        if (i < ncl) {
            cfs   = bs->cf_32[dts[COEFFS]];
            /* set corresponding bit of reducer in reducer bit array */
            rba[i/32] |= 1U << (i % 32);
        } else {
            cfs   = mcf[dts[COEFFS]];
        }

#ifdef HAVE_AVX2
        const len_t len = dts[LENGTH];
        const len_t os  = len % 8;
        const hm_t * const ds  = dts + OFFSET;
        const uint32_t mul32 = (uint32_t)(dr[i]);
        mulv  = _mm256_set1_epi32(mul32);
        for (j = 0; j < os; ++j) {
            dr[ds[j]] -=  mul * cfs[j];
            dr[ds[j]] +=  (dr[ds[j]] >> 63) & mod2;
        }
        for (; j < len; j += 8) {
            redv  = _mm256_loadu_si256((__m256i*)(cfs+j));
            drv   = _mm256_setr_epi64x(
                dr[ds[j+1]],
                dr[ds[j+3]],
                dr[ds[j+5]],
                dr[ds[j+7]]);
            /* first four mult-adds -- lower */
            prodv = _mm256_mul_epu32(mulv, _mm256_srli_epi64(redv, 32));
            resv  = _mm256_sub_epi64(drv, prodv);
            cmpv  = _mm256_cmpgt_epi64(zerov, resv);
            rresv = _mm256_add_epi64(resv, _mm256_and_si256(cmpv, mod2v));
            _mm256_store_si256((__m256i*)(res), rresv);
            dr[ds[j+1]] = res[0];
            dr[ds[j+3]] = res[1];
            dr[ds[j+5]] = res[2];
            dr[ds[j+7]] = res[3];
            /* second four mult-adds -- higher */
            prodv = _mm256_mul_epu32(mulv, redv);
            drv   = _mm256_setr_epi64x(
                dr[ds[j]],
                dr[ds[j+2]],
                dr[ds[j+4]],
                dr[ds[j+6]]);
            resv  = _mm256_sub_epi64(drv, prodv);
            cmpv  = _mm256_cmpgt_epi64(zerov, resv);
            rresv = _mm256_add_epi64(resv, _mm256_and_si256(cmpv, mod2v));
            _mm256_store_si256((__m256i*)(res), rresv);
            dr[ds[j]]   = res[0];
            dr[ds[j+2]] = res[1];
            dr[ds[j+4]] = res[2];
            dr[ds[j+6]] = res[3];
        }
#else
        const len_t os  = dts[PRELOOP];
        const len_t len = dts[LENGTH];
        const hm_t * const ds = dts + OFFSET;
        for (j = 0; j < os; ++j) {
            dr[ds[j]] -=  mul * cfs[j];
            dr[ds[j]] +=  (dr[ds[j]] >> 63) & mod2;
        }
        for (; j < len; j += UNROLL) {
            dr[ds[j]]   -=  mul * cfs[j];
            dr[ds[j+1]] -=  mul * cfs[j+1];
            dr[ds[j+2]] -=  mul * cfs[j+2];
            dr[ds[j+3]] -=  mul * cfs[j+3];
            dr[ds[j]]   +=  (dr[ds[j]] >> 63) & mod2;
            dr[ds[j+1]] +=  (dr[ds[j+1]] >> 63) & mod2;
            dr[ds[j+2]] +=  (dr[ds[j+2]] >> 63) & mod2;
            dr[ds[j+3]] +=  (dr[ds[j+3]] >> 63) & mod2;
        }
#endif
        dr[i] = 0;
        st->trace_nr_mult +=  len / 1000.0;
        st->trace_nr_add  +=  len / 1000.0;
        st->trace_nr_red++;
    }

    if (k == 0) {
        return NULL;
    }

    hm_t *row   = (hm_t *)malloc((unsigned long)(k+OFFSET) * sizeof(hm_t));
    cf32_t *cf  = (cf32_t *)malloc((unsigned long)(k) * sizeof(cf32_t));
    j = 0;
    hm_t *rs  = row + OFFSET;
    for (i = ncl; i < ncols; ++i) {
        if (dr[i] != 0) {
            rs[j] = (hm_t)i;
            cf[j] = (cf32_t)dr[i];
            j++;
        }
    }
    row[BINDEX]   = bi;
    row[MULT]     = mh;
    row[COEFFS]   = tmp_pos;
    row[PRELOOP]  = j % UNROLL;
    row[LENGTH]   = j;
    mat->cf_32[tmp_pos]  = cf;

    return row;
}


static hm_t *reduce_dense_row_by_known_pivots_sparse_32_bit(
        int64_t *dr,
        mat_t *mat,
        const bs_t * const bs,
        hm_t *const *pivs,
        const hi_t dpiv,    /* pivot of dense row at the beginning */
        const hm_t tmp_pos, /* position of new coeffs array in tmpcf */
        stat_t *st
        )
{
    hi_t i, j, k;
    cf32_t *cfs;
    hm_t *dts;
    int64_t np = -1;
    const uint64_t mod          = (uint64_t)st->fc;
    const len_t ncols           = mat->nc;
    const len_t ncl             = mat->ncl;
    cf32_t * const * const mcf  = mat->cf_32;
    const uint64_t mask = (uint32_t)0xFFFFFFFF;
    uint64_t RED_32     = ((uint64_t)2<<31) % st->fc;
    uint64_t RED_64     = ((uint64_t)1<<63) % st->fc;
    RED_64              = (RED_64*2) % st->fc;
    uint64_t drlow[ncols];
    uint64_t drhigh[ncols];
    uint64_t udr[ncols];
    uint64_t prod;
    for (i = 0; i < ncols; ++i) {
      drlow[i]  = dr[i] & mask;
      drhigh[i] = dr[i] >> 32;
    }

    k = 0;
    for (i = dpiv; i < ncols; ++i) {
        udr[i] =   ((drhigh[i] >> 32) * RED_64) % mod;
        udr[i] +=  ((drhigh[i] & (uint64_t)0xFFFFFFFF) * RED_32) % mod;
        udr[i] += drlow[i];
        udr[i] %=  mod;
        if (udr[i] == 0) {
            continue;
        }
        if (pivs[i] == NULL) {
            if (np == -1) {
                np  = i;
            }
            k++;
            continue;
        }

        /* found reducer row, get multiplier */
        const uint64_t mul = mod-udr[i];
        dts   = pivs[i];
        if (i < ncl) {
            cfs   = bs->cf_32[dts[COEFFS]];
        } else {
            cfs   = mcf[dts[COEFFS]];
        }
        const len_t len = dts[LENGTH];
        const hm_t * const ds = dts + OFFSET;
        /* loop unrolling is slower */
        for (j = 0; j < len; ++j) {
            prod  =  mul * cfs[j];
            drhigh[ds[j]] +=  prod >> 32;
            drlow[ds[j]]  +=  (prod & mask);
        }
        udr[i]  = 0;
        st->application_nr_mult +=  len / 1000.0;
        st->application_nr_add  +=  len / 1000.0;
        st->application_nr_red++;
    }

    if (k == 0) {
        return NULL;
    }

    hm_t *row   = (hm_t *)malloc((unsigned long)(k+OFFSET) * sizeof(hm_t));
    cf32_t *cf  = (cf32_t *)malloc((unsigned long)(k) * sizeof(cf32_t));
    j = 0;
    hm_t *rs  = row + OFFSET;
    for (i = np; i < ncols; ++i) {
        if (udr[i] != 0) {
            rs[j] = (hm_t)i;
            cf[j] = (cf32_t)udr[i];
            j++;
        }
    }
    row[COEFFS]         = tmp_pos;
    row[PRELOOP]        = j % UNROLL;
    row[LENGTH]         = j;
    mat->cf_32[tmp_pos] = cf;

    return row;
}

static hm_t *trace_reduce_dense_row_by_known_pivots_sparse_32_bit(
        rba_t *rba,
        int64_t *dr,
        mat_t *mat,
        const bs_t * const bs,
        hm_t *const *pivs,
        const hi_t dpiv,    /* pivot of dense row at the beginning */
        const hm_t tmp_pos, /* position of new coeffs array in tmpcf */
        const len_t mh,     /* multiplier hash for tracing */
        const len_t bi,     /* basis index of generating element */
        stat_t *st
        )
{
    hi_t i, j, k;
    cf32_t *cfs;
    hm_t *dts;
    int64_t np = -1;
    const uint64_t mod          = (uint64_t)st->fc;
    const len_t ncols           = mat->nc;
    const len_t ncl             = mat->ncl;
    cf32_t * const * const mcf  = mat->cf_32;
    const uint64_t mask = (uint32_t)0xFFFFFFFF;
    uint64_t RED_32     = ((uint64_t)2<<31) % st->fc;
    uint64_t RED_64     = ((uint64_t)1<<63) % st->fc;
    RED_64              = (RED_64*2) % st->fc;
    uint64_t drlow[ncols];
    uint64_t drhigh[ncols];
    uint64_t udr[ncols];
    uint64_t prod;
    for (i = 0; i < ncols; ++i) {
      drlow[i]  = dr[i] & mask;
      drhigh[i] = dr[i] >> 32;
    }

    k = 0;
    for (i = dpiv; i < ncols; ++i) {
        udr[i] =   ((drhigh[i] >> 32) * RED_64) % mod;
        udr[i] +=  ((drhigh[i] & (uint64_t)0xFFFFFFFF) * RED_32) % mod;
        udr[i] += drlow[i];
        udr[i] %=  mod;
        if (udr[i] == 0) {
            continue;
        }
        if (pivs[i] == NULL) {
            if (np == -1) {
                np  = i;
            }
            k++;
            continue;
        }

        /* found reducer row, get multiplier */
        const uint64_t mul = mod-udr[i];
        dts   = pivs[i];
        if (i < ncl) {
            cfs   = bs->cf_32[dts[COEFFS]];
        } else {
            cfs   = mcf[dts[COEFFS]];
        }
        const len_t len = dts[LENGTH];
        const hm_t * const ds = dts + OFFSET;
        /* loop unrolling is slower */
        for (j = 0; j < len; ++j) {
            prod  =  mul * cfs[j];
            drhigh[ds[j]] +=  prod >> 32;
            drlow[ds[j]]  +=  (prod & mask);
        }
        udr[i]  = 0;
        st->trace_nr_mult +=  len / 1000.0;
        st->trace_nr_add  +=  len / 1000.0;
        st->trace_nr_red++;
    }

    if (k == 0) {
        return NULL;
    }

    hm_t *row   = (hm_t *)malloc((unsigned long)(k+OFFSET) * sizeof(hm_t));
    cf32_t *cf  = (cf32_t *)malloc((unsigned long)(k) * sizeof(cf32_t));
    j = 0;
    hm_t *rs  = row + OFFSET;
    for (i = np; i < ncols; ++i) {
        if (udr[i] != 0) {
            rs[j] = (hm_t)i;
            cf[j] = (cf32_t)udr[i];
            j++;
        }
    }
    row[BINDEX]   = bi;
    row[MULT]     = mh;
    row[COEFFS]   = tmp_pos;
    row[PRELOOP]  = j % UNROLL;
    row[LENGTH]   = j;
    mat->cf_32[tmp_pos]  = cf;

    return row;
}

static cf32_t *reduce_dense_row_by_all_pivots_17_bit(
        int64_t *dr,
        mat_t *mat,
        const bs_t * const bs,
        len_t *pc,
        hm_t *const * const pivs,
        cf32_t *const * const dpivs,
        const uint32_t fc
        )
{
    hi_t i, j, k, l;
    const int64_t mod = (int64_t)fc;
    len_t np  = -1;
    cf32_t *red;

    const len_t ncl   = mat->ncl;
    const len_t ncols = mat->nc;

    /* step 1: reduce by sparse known pivots */
    for (i = *pc; i < ncl; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        if (dr[i] == 0) {
            continue;
        }
        if (pivs[i] == NULL) {
            continue;
        }

        /* found reducer row, get multiplier */
        const int64_t mul = mod - dr[i];
        const cf32_t *cfs = bs->cf_32[pivs[i][COEFFS]];
        const len_t os    = pivs[i][PRELOOP];
        const len_t len   = pivs[i][LENGTH];
        const hm_t * const ds  = pivs[i] + OFFSET;
        for (j = 0; j < os; ++j) {
            dr[ds[j]] +=  mul * cfs[j];
        }
        for (; j < len; j += UNROLL) {
            dr[ds[j]]   +=  mul * cfs[j];
            dr[ds[j+1]] +=  mul * cfs[j+1];
            dr[ds[j+2]] +=  mul * cfs[j+2];
            dr[ds[j+3]] +=  mul * cfs[j+3];
        }
        dr[i] = 0;
    }
    k = 0;
    /* step 2: reduce by new dense pivots */
    for (i = ncl; i < ncols; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        if (dr[i] == 0) {
            continue;
        }
        if (dpivs[i-ncl] == NULL) {
            if (np == -1) {
                np  = i;
            }
            k++;
            continue;
        }

        red = dpivs[i-ncl];
        const int64_t mul = mod - dr[i];
        const len_t os    = (ncols - i) % UNROLL;
        for (l = 0, j = i; l < os; ++l, ++j) {
            dr[j] +=  mul * red[l];
        }
        for (; j < ncols; l += UNROLL, j += UNROLL) {
            dr[j]   +=  mul * red[l];
            dr[j+1] +=  mul * red[l+1];
            dr[j+2] +=  mul * red[l+2];
            dr[j+3] +=  mul * red[l+3];
        }
    }
    if (k == 0) {
        *pc = -1;
        return NULL;
    }

    cf32_t *row = (cf32_t *)calloc((unsigned long)(ncols-np), sizeof(cf32_t));
    for (i = np; i < ncols; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        row[i-np]  = (cf32_t)dr[i];
    }
    if (row[0] != 1) {
        row = normalize_dense_matrix_row_ff_32(row, ncols-np, fc);
    }

    *pc = np - ncl;
    return row;
}

static cf32_t *reduce_dense_row_by_all_pivots_31_bit(
        int64_t *dr,
        mat_t *mat,
        const bs_t * const bs,
        len_t *pc,
        hm_t * const * const pivs,
        cf32_t * const * const dpivs,
        const uint32_t fc
        )
{
    hi_t i, j, k, l;
    const int64_t mod   = (int64_t)fc;
    const int64_t mod2  = (int64_t)fc * fc;
    len_t np  = -1;
    cf32_t *red;

    const len_t ncl   = mat->ncl;
    const len_t ncols = mat->nc;

    /* step 1: reduce by sparse known pivots */
    for (i = *pc; i < ncl; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        if (dr[i] == 0) {
            continue;
        }
        if (pivs[i] == NULL) {
            continue;
        }

        /* found reducer row, get multiplier */
        const int64_t mul = (int64_t)dr[i];
        const cf32_t *cfs = bs->cf_32[pivs[i][COEFFS]];
        const len_t os    = pivs[i][PRELOOP];
        const len_t len   = pivs[i][LENGTH];
        const hm_t * const ds = pivs[i] + OFFSET;
        for (j = 0; j < os; ++j) {
            dr[ds[j]] -=  mul * cfs[j];
            dr[ds[j]] +=  (dr[ds[j]] >> 63) & mod2;
        }
        for (; j < len; j += UNROLL) {
            dr[ds[j]]   -=  mul * cfs[j];
            dr[ds[j+1]] -=  mul * cfs[j+1];
            dr[ds[j+2]] -=  mul * cfs[j+2];
            dr[ds[j+3]] -=  mul * cfs[j+3];
            dr[ds[j]]   +=  (dr[ds[j]] >> 63) & mod2;
            dr[ds[j+1]] +=  (dr[ds[j+1]] >> 63) & mod2;
            dr[ds[j+2]] +=  (dr[ds[j+2]] >> 63) & mod2;
            dr[ds[j+3]] +=  (dr[ds[j+3]] >> 63) & mod2;
        }
        dr[i] = 0;
    }
    k = 0;
    /* step 2: reduce by new dense pivots */
    for (i = ncl; i < ncols; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        if (dr[i] == 0) {
            continue;
        }
        if (dpivs[i-ncl] == NULL) {
            if (np == -1) {
                np  = i;
            }
            k++;
            continue;
        }

        red = dpivs[i-ncl];
        const int64_t mul = (int64_t) dr[i];
        const len_t os    = (ncols - i) % UNROLL;
        for (l = 0, j = i; l < os; ++l, ++j) {
            dr[j] -=  mul * red[l];
            dr[j] +=  (dr[j] >> 63) & mod2;
        }
        for (; j < ncols; l+=4, j += UNROLL) {
            dr[j]   -=  mul * red[l];
            dr[j+1] -=  mul * red[l+1];
            dr[j+2] -=  mul * red[l+2];
            dr[j+3] -=  mul * red[l+3];
            dr[j]   +=  (dr[j] >> 63) & mod2;
            dr[j+1] +=  (dr[j+1] >> 63) & mod2;
            dr[j+2] +=  (dr[j+2] >> 63) & mod2;
            dr[j+3] +=  (dr[j+3] >> 63) & mod2;
        }
    }
    if (k == 0) {
        *pc = -1;
        return NULL;
    }

    cf32_t *row = (cf32_t *)calloc((unsigned long)(ncols-np), sizeof(cf32_t));
    for (i = np; i < ncols; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        row[i-np]  = (cf32_t)dr[i];
    }
    if (row[0] != 1) {
        row = normalize_dense_matrix_row_ff_32(row, ncols-np, fc);
    }

    *pc = np - ncl;

    return row;
}

static cf32_t *reduce_dense_row_by_old_pivots_17_bit(
        int64_t *dr,
        mat_t *mat,
        const bs_t * const bs,
        hm_t *const * const pivs,
        const hi_t dpiv,    /* pivot of dense row at the beginning */
        const uint32_t fc
        )
{
    hi_t i, j;
    const int64_t mod = (int64_t)fc;
    const len_t ncols = mat->nc;
    const len_t ncl   = mat->ncl;
    const len_t ncr   = mat->ncr;

    for (i = dpiv; i < ncl; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        if (dr[i] == 0) {
            continue;
        }
        if (pivs[i] == NULL) {
            continue;
        }

        /* found reducer row, get multiplier */
        const int64_t mul = mod - dr[i];
        const cf32_t *cfs = bs->cf_32[pivs[i][COEFFS]];
        const len_t os    = pivs[i][PRELOOP];
        const len_t len   = pivs[i][LENGTH];
        const hm_t * const ds = pivs[i] + OFFSET;
        for (j = 0; j < os; ++j) {
            dr[ds[j]] +=  mul * cfs[j];
        }
        for (; j < len; j += UNROLL) {
            dr[ds[j]]   +=  mul * cfs[j];
            dr[ds[j+1]] +=  mul * cfs[j+1];
            dr[ds[j+2]] +=  mul * cfs[j+2];
            dr[ds[j+3]] +=  mul * cfs[j+3];
        }
        dr[i] = 0;
    }

    /* store a dense row for further dense gaussian elimination */
    cf32_t *row  = (cf32_t *)calloc(
            (unsigned long)(ncr), sizeof(cf32_t));
    j = 0;
    for (i = ncl; i < ncols; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
            if (dr[i] != 0) {
                j++;
                row[i-ncl]  = (cf32_t)dr[i];
            }
        }
    }
    if (j == 0) {
        free(row);
        row = NULL;
    }
    return row;
}

static cf32_t *reduce_dense_row_by_old_pivots_31_bit(
        int64_t *dr,
        mat_t *mat,
        const bs_t * const bs,
        hm_t *const *pivs,
        const hi_t dpiv,  /* pivot of dense row at the beginning */
        const uint32_t fc
        )
{
    hi_t i, j;
    const int64_t mod   = (int64_t)fc;
    const int64_t mod2  = (int64_t)fc * fc;
    const len_t ncols   = mat->nc;
    const len_t ncl     = mat->ncl;
    const len_t ncr     = mat->ncr;

    for (i = dpiv; i < ncl; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        if (dr[i] == 0) {
            continue;
        }
        if (pivs[i] == NULL) {
            continue;
        }

        /* found reducer row, get multiplier */
        const int64_t mul = (int64_t)dr[i];
        const cf32_t *cfs = bs->cf_32[pivs[i][COEFFS]];
        const len_t os    = pivs[i][PRELOOP];
        const len_t len   = pivs[i][LENGTH];
        const hm_t * const ds = pivs[i] + OFFSET;
        for (j = 0; j < os; ++j) {
            dr[ds[j]] -=  mul * cfs[j];
            dr[ds[j]] +=  (dr[ds[j]] >> 63) & mod2;
        }
        for (; j < len; j += UNROLL) {
            dr[ds[j]]   -=  mul * cfs[j];
            dr[ds[j+1]] -=  mul * cfs[j+1];
            dr[ds[j+2]] -=  mul * cfs[j+2];
            dr[ds[j+3]] -=  mul * cfs[j+3];
            dr[ds[j]]   +=  (dr[ds[j]] >> 63) & mod2;
            dr[ds[j+1]] +=  (dr[ds[j+1]] >> 63) & mod2;
            dr[ds[j+2]] +=  (dr[ds[j+2]] >> 63) & mod2;
            dr[ds[j+3]] +=  (dr[ds[j+3]] >> 63) & mod2;
        }
        dr[i] = 0;
    }

    /* store a dense row for further dense gaussian elimination */
    cf32_t *row  = (cf32_t *)calloc(
            (unsigned long)(ncr), sizeof(cf32_t));

    j = 0;
    for (i = ncl; i < ncols; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
            if (dr[i] != 0) {
                j++;
                row[i-ncl]  = (cf32_t)dr[i];
            }
        }
    }
    if (j == 0) {
        free(row);
        row = NULL;
    }

    return row;
}

static cf32_t *reduce_dense_row_by_dense_new_pivots_17_bit(
        int64_t *dr,
        len_t *pc,
        cf32_t * const * const pivs,
        const len_t ncr,
        const uint32_t fc
        )
{
    hi_t i, j, k, l;
    len_t np  = -1;
    const int64_t mod = (int64_t)fc;

    for (k = 0, i = *pc; i < ncr; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        if (dr[i] == 0) {
            continue;
        }
        if (pivs[i] == NULL) {
            if (np == -1) {
                np  = i;
            }
            k++;
            continue;
        }

        const int64_t mul = mod - dr[i];
        const len_t os    = (ncr - i) % UNROLL;
        for (l = 0, j = i; l < os; ++l, ++j) {
            dr[j] +=  mul * pivs[i][l];
        }
        for (; j < ncr; l += UNROLL, j += UNROLL) {
            dr[j]   +=  mul * pivs[i][l];
            dr[j+1] +=  mul * pivs[i][l+1];
            dr[j+2] +=  mul * pivs[i][l+2];
            dr[j+3] +=  mul * pivs[i][l+3];
        }
    }
    if (k == 0) {
        *pc = -1;
        return NULL;
    }

    cf32_t *row = (cf32_t *)calloc((unsigned long)(ncr-np), sizeof(cf32_t));
    for (i = np; i < ncr; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        row[i-np]  = (cf32_t)dr[i];
    }
    if (row[0] != 1) {
        row = normalize_dense_matrix_row_ff_32(row, ncr-np, fc);
    }
    *pc = np;
    return row;
}

static cf32_t *reduce_dense_row_by_dense_new_pivots_31_bit(
        int64_t *dr,
        len_t *pc,
        cf32_t * const * const pivs,
        const len_t ncr,
        const uint32_t fc
        )
{
    hi_t i, j, k, l;
    len_t np  = -1;
    const int64_t mod = (int64_t)fc;
    const int64_t mod2  = (int64_t)fc * fc;

    for (k = 0, i = *pc; i < ncr; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        if (dr[i] == 0) {
            continue;
        }
        if (pivs[i] == NULL) {
            if (np == -1) {
                np  = i;
            }
            k++;
            continue;
        }

        const int64_t mul = (int64_t)dr[i];
        const len_t os    = (ncr - i) % UNROLL;
        for (l = 0, j = i; l < os; ++l, ++j) {
            dr[j] -=  mul * pivs[i][l];
            dr[j] +=  (dr[j] >> 63) & mod2;
        }
        for (; j < ncr; l+=4, j += UNROLL) {
            dr[j]   -=  mul * pivs[i][l];
            dr[j+1] -=  mul * pivs[i][l+1];
            dr[j+2] -=  mul * pivs[i][l+2];
            dr[j+3] -=  mul * pivs[i][l+3];
            dr[j]   +=  (dr[j] >> 63) & mod2;
            dr[j+1] +=  (dr[j+1] >> 63) & mod2;
            dr[j+2] +=  (dr[j+2] >> 63) & mod2;
            dr[j+3] +=  (dr[j+3] >> 63) & mod2;
        }
    }
    if (k == 0) {
        *pc = -1;
        return NULL;
    }

    cf32_t *row = (cf32_t *)calloc((unsigned long)(ncr-np), sizeof(cf32_t));
    for (i = np; i < ncr; ++i) {
        if (dr[i] != 0) {
            dr[i] = dr[i] % mod;
        }
        row[i-np]  = (cf32_t)dr[i];
    }
    if (row[0] != 1) {
        row = normalize_dense_matrix_row_ff_32(row, ncr-np, fc);
    }
    *pc = np;

    return row;
}

static void probabilistic_sparse_reduced_echelon_form_ff_32(
        mat_t *mat,
        const bs_t * const bs,
        stat_t *st
        )
{
    len_t i = 0, j, k, l, m;

    const len_t ncols = mat->nc;
    const len_t nrl   = mat->nrl;
    const len_t ncr   = mat->ncr;
    const len_t ncl   = mat->ncl;

    /* we fill in all known lead terms in pivs */
    hm_t **pivs   = (hm_t **)calloc((unsigned long)ncols, sizeof(hm_t *));
    memcpy(pivs, mat->rr, (unsigned long)mat->nru * sizeof(hm_t *));

    /* unkown pivot rows we have to reduce with the known pivots first */
    hm_t **upivs  = mat->tr;

    const uint32_t fc   = st->fc;
    /* Why do we generate the random linear combinations so strangely
     * compared to, e.g. la_ff_8.c or la_ff_16.c?
     * We can have fc > 2^31, so shifting the result of random multiplier
     * and coefficient may lead to elements of size > 2^63 which cannot
     * be represented by int64_t anymore. Thus we restrict the multiplier
     * via masking with mask to < 2^31. Then, after subtracting the
     * multiplied values we have to check if at some point the result gets
     * negative as int64_t, i.e. if drl[i] >> 63 is 1. If so, we add a
     * corresponding multiple of fc. Again, if fc > 2^31 we cannot just
     * add fc * fc, but we have to restrict to fc/2 * fc.
     * Note: We cannot apply this trick to the reduction itself, there we
     * cannot restrict the multiplier to be < 2^31, so we have to handle the
     * fc > 2^31 case differently. */
    uint64_t tmp  = (uint64_t)fc * fc;
    while (tmp > pow(2,63)) {
      tmp -=  (uint64_t)(fc/2) * fc;
    }
    const int64_t mod2  = tmp;
    /* compute rows per block */
    const len_t nb  = (len_t)(floor(sqrt(nrl/3)))+1;
    const len_t rem = (nrl % nb == 0) ? 0 : 1;
    const len_t rpb = (nrl / nb) + rem;
    const int64_t mask  = pow(2,(uint32_t)(ceil(log((double)st->max_uht_size)/log(2))))-1;

    int64_t *dr   = (int64_t *)malloc(
        (unsigned long)(st->nthrds * ncols) * sizeof(int64_t));
    int64_t *mul  = (int64_t *)malloc(
        (unsigned long)(st->nthrds * rpb) * sizeof(int64_t));

    /* mo need to have any sharing dependencies on parallel computation,
     * no data to be synchronized at this step of the linear algebra */
#pragma omp parallel for num_threads(st->nthrds) \
    private(i, j, k, l, m) \
    schedule(dynamic)
    for (i = 0; i < nb; ++i) {
        int64_t *drl  = dr + (omp_get_thread_num() * ncols);
        int64_t *mull = mul + (omp_get_thread_num() * rpb);
        const int32_t nbl   = (int32_t) (nrl > (i+1)*rpb ? (i+1)*rpb : nrl);
        const int32_t nrbl  = (int32_t) (nbl - i*rpb);
        if (nrbl != 0) {
            hm_t *npiv  = NULL;
            cf32_t *cfs;
            /* starting column, offset, coefficient array position in tmpcf */
            hm_t sc, cfp;
            len_t bctr  = 0;
            while (bctr < nrbl) {
                cfp = bctr + i*rpb;
                sc  = 0;

                /* fill random value array */
                for (j = 0; j < nrbl; ++j) {
                    mull[j] = (int64_t)rand() & mask;
                }
                /* generate one dense row as random linear combination
                 * of the rows of the block */
                memset(drl, 0, (unsigned long)ncols * sizeof(int64_t));

                for (k = 0, m = i*rpb; m < nbl; ++k, ++m) {
                    npiv  = upivs[m];
                    cfs   = bs->cf_32[npiv[COEFFS]];
                    const len_t os  = npiv[PRELOOP];
                    const len_t len = npiv[LENGTH];
                    const hm_t * const ds = npiv + OFFSET;
                    sc    = sc < ds[0] ? sc : ds[0];
                    for (l = 0; l < os; ++l) {
                        drl[ds[l]]  -=  mull[k] * cfs[l];
                        drl[ds[l]]  +=  (drl[ds[l]] >> 63) & mod2;
                    }
                    for (; l < len; l += UNROLL) {
                        drl[ds[l]]    -=  mull[k] * cfs[l];
                        drl[ds[l]]    +=  (drl[ds[l]] >> 63) & mod2;
                        drl[ds[l+1]]  -=  mull[k] * cfs[l+1];
                        drl[ds[l+1]]  +=  (drl[ds[l+1]] >> 63) & mod2;
                        drl[ds[l+2]]  -=  mull[k] * cfs[l+2];
                        drl[ds[l+2]]  +=  (drl[ds[l+2]] >> 63) & mod2;
                        drl[ds[l+3]]  -=  mull[k] * cfs[l+3];
                        drl[ds[l+3]]  +=  (drl[ds[l+3]] >> 63) & mod2;
                    }
                }
                k     = 0;
                cfs   = NULL;
                npiv  = NULL;
                /* do the reduction */
                do {
                    free(cfs);
                    cfs = NULL;
                    free(npiv);
                    npiv  = NULL;
                    npiv  = reduce_dense_row_by_known_pivots_sparse_ff_32(
                            drl, mat, bs, pivs, sc, cfp, st);
                    if (!npiv) {
                        bctr  = nrbl;
                        break;
                    }
                    /* normalize coefficient array
                    * NOTE: this has to be done here, otherwise the reduction may
                    * lead to wrong results in a parallel computation since other
                    * threads might directly use the new pivot once it is synced. */
                    if (mat->cf_32[npiv[COEFFS]][0] != 1) {
                        normalize_sparse_matrix_row_ff_32(
                                mat->cf_32[npiv[COEFFS]], npiv[PRELOOP], npiv[LENGTH], st->fc);
                    }
                    cfs = mat->cf_32[npiv[COEFFS]];
                    sc  = npiv[OFFSET];
                    k   = __sync_bool_compare_and_swap(&pivs[npiv[OFFSET]], NULL, npiv);
                } while (!k);
                bctr++;
            }
            for (j = i*rpb; j < nbl; ++j) {
                free(upivs[j]);
                upivs[j]  = NULL;
            }
        }
    }
    free(mul);
    mul   = NULL;

    /* we do not need the old pivots anymore */
    for (i = 0; i < ncl; ++i) {
        free(pivs[i]);
        pivs[i] = NULL;
    }

    len_t npivs = 0; /* number of new pivots */

    dr      = realloc(dr, (unsigned long)ncols * sizeof(int64_t));
    mat->tr = realloc(mat->tr, (unsigned long)ncr * sizeof(hm_t *));

    /* interreduce new pivots */
    cf32_t *cfs;
    /* starting column, coefficient array position in tmpcf */
    hm_t sc, cfp;
    for (i = 0; i < ncr; ++i) {
        k = ncols-1-i;
        if (pivs[k]) {
            memset(dr, 0, (unsigned long)ncols * sizeof(int64_t));
            cfs = mat->cf_32[pivs[k][COEFFS]];
            cfp = pivs[k][COEFFS];
            const len_t os  = pivs[k][PRELOOP];
            const len_t len = pivs[k][LENGTH];
            const hm_t * const ds = pivs[k] + OFFSET;
            sc  = ds[0];
            for (j = 0; j < os; ++j) {
                dr[ds[j]] = (int64_t)cfs[j];
            }
            for (; j < len; j += UNROLL) {
                dr[ds[j]]   = (int64_t)cfs[j];
                dr[ds[j+1]] = (int64_t)cfs[j+1];
                dr[ds[j+2]] = (int64_t)cfs[j+2];
                dr[ds[j+3]] = (int64_t)cfs[j+3];
            }
            free(pivs[k]);
            free(cfs);
            pivs[k] = NULL;
            pivs[k] = mat->tr[npivs++] =
                reduce_dense_row_by_known_pivots_sparse_ff_32(
                        dr, mat, bs, pivs, sc, cfp, st);
        }
    }
    free(mat->rr);
    mat->rr = NULL;

    free(pivs);
    pivs  = NULL;

    free(dr);
    dr  = NULL;

    mat->tr = realloc(mat->tr, (unsigned long)npivs * sizeof(hi_t *));
    mat->np = mat->nr = mat->sz = npivs;
}

static void exact_sparse_reduced_echelon_form_ff_32(
        mat_t *mat,
        const bs_t * const bs,
        stat_t *st
        )
{
    len_t i = 0, j, k;
    hi_t sc = 0;    /* starting column */

    const len_t ncols = mat->nc;
    const len_t nrl   = mat->nrl;
    const len_t ncr   = mat->ncr;
    const len_t ncl   = mat->ncl;

    /* we fill in all known lead terms in pivs */
    hm_t **pivs   = (hm_t **)calloc((unsigned long)ncols, sizeof(hm_t *));
    memcpy(pivs, mat->rr, (unsigned long)mat->nru * sizeof(hm_t *));

    /* unkown pivot rows we have to reduce with the known pivots first */
    hm_t **upivs  = mat->tr;

    int64_t *dr  = (int64_t *)malloc(
            (unsigned long)(st->nthrds * ncols) * sizeof(int64_t));
    /* mo need to have any sharing dependencies on parallel computation,
     * no data to be synchronized at this step of the linear algebra */
#pragma omp parallel for num_threads(st->nthrds) \
    private(i, j, k, sc) \
    schedule(dynamic)
    for (i = 0; i < nrl; ++i) {
        int64_t *drl  = dr + (omp_get_thread_num() * ncols);
        hm_t *npiv      = upivs[i];
        cf32_t *cfs     = bs->cf_32[npiv[COEFFS]];
        const len_t os  = npiv[PRELOOP];
        const len_t len = npiv[LENGTH];
        const hm_t * const ds = npiv + OFFSET;
        k = 0;
        memset(drl, 0, (unsigned long)ncols * sizeof(int64_t));
        for (j = 0; j < os; ++j) {
            drl[ds[j]]  = (int64_t)cfs[j];
        }
        for (; j < len; j += UNROLL) {
            drl[ds[j]]    = (int64_t)cfs[j];
            drl[ds[j+1]]  = (int64_t)cfs[j+1];
            drl[ds[j+2]]  = (int64_t)cfs[j+2];
            drl[ds[j+3]]  = (int64_t)cfs[j+3];
        }
        cfs = NULL;
        do {
            sc  = npiv[OFFSET];
            free(npiv);
            free(cfs);
            npiv  = reduce_dense_row_by_known_pivots_sparse_ff_32(
                    drl, mat, bs, pivs, sc, i, st);
            if (!npiv) {
                break;
            }
            /* normalize coefficient array
             * NOTE: this has to be done here, otherwise the reduction may
             * lead to wrong results in a parallel computation since other
             * threads might directly use the new pivot once it is synced. */
            if (mat->cf_32[npiv[COEFFS]][0] != 1) {
                normalize_sparse_matrix_row_ff_32(
                        mat->cf_32[npiv[COEFFS]], npiv[PRELOOP], npiv[LENGTH], st->fc);
            }
            k   = __sync_bool_compare_and_swap(&pivs[npiv[OFFSET]], NULL, npiv);
            cfs = mat->cf_32[npiv[COEFFS]];
        } while (!k);
    }

    /* we do not need the old pivots anymore */
    for (i = 0; i < ncl; ++i) {
        free(pivs[i]);
        pivs[i] = NULL;
    }

    len_t npivs = 0; /* number of new pivots */

    dr      = realloc(dr, (unsigned long)ncols * sizeof(int64_t));
    mat->tr = realloc(mat->tr, (unsigned long)ncr * sizeof(hm_t *));

    /* interreduce new pivots */
    cf32_t *cfs;
    hm_t cf_array_pos;
    for (i = 0; i < ncr; ++i) {
        k = ncols-1-i;
        if (pivs[k]) {
            memset(dr, 0, (unsigned long)ncols * sizeof(int64_t));
            cfs = mat->cf_32[pivs[k][COEFFS]];
            cf_array_pos    = pivs[k][COEFFS];
            const len_t os  = pivs[k][PRELOOP];
            const len_t len = pivs[k][LENGTH];
            const hm_t * const ds = pivs[k] + OFFSET;
            sc  = ds[0];
            for (j = 0; j < os; ++j) {
                dr[ds[j]] = (int64_t)cfs[j];
            }
            for (; j < len; j += UNROLL) {
                dr[ds[j]]    = (int64_t)cfs[j];
                dr[ds[j+1]]  = (int64_t)cfs[j+1];
                dr[ds[j+2]]  = (int64_t)cfs[j+2];
                dr[ds[j+3]]  = (int64_t)cfs[j+3];
            }
            free(pivs[k]);
            free(cfs);
            pivs[k] = NULL;
            pivs[k] = mat->tr[npivs++] =
                reduce_dense_row_by_known_pivots_sparse_ff_32(
                        dr, mat, bs, pivs, sc, cf_array_pos, st);
        }
    }
    free(pivs);
    pivs  = NULL;
    free(dr);
    dr  = NULL;

    mat->tr = realloc(mat->tr, (unsigned long)npivs * sizeof(hi_t *));
    mat->np = mat->nr = mat->sz = npivs;
}

static void exact_sparse_reduced_echelon_form_nf_ff_32(
        mat_t *mat,
        const bs_t * const tbr,
        const bs_t * const bs,
        stat_t *st
        )
{
    len_t i = 0, j;
    hi_t sc = 0;    /* starting column */

    const len_t ncols = mat->nc;
    const len_t nrl   = mat->nrl;
    const len_t ncl   = mat->ncl;

    /* we fill in all known lead terms in pivs */
    hm_t **pivs   = (hm_t **)calloc((unsigned long)ncols, sizeof(hm_t *));
    memcpy(pivs, mat->rr, (unsigned long)mat->nru * sizeof(hm_t *));

    /* unkown pivot rows we have to reduce with the known pivots first */
    hm_t **upivs  = mat->tr;

    int64_t *dr  = (int64_t *)malloc(
            (unsigned long)(st->nthrds * ncols) * sizeof(int64_t));
    /* mo need to have any sharing dependencies on parallel computation,
     * no data to be synchronized at this step of the linear algebra */
#pragma omp parallel for num_threads(st->nthrds) \
    private(i, j, sc) \
    schedule(dynamic)
    for (i = 0; i < nrl; ++i) {
        int64_t *drl  = dr + (omp_get_thread_num() * ncols);
        hm_t *npiv      = upivs[i];
        cf32_t *cfs     = tbr->cf_32[npiv[COEFFS]];
        const len_t os  = npiv[PRELOOP];
        const len_t len = npiv[LENGTH];
        const hm_t * const ds = npiv + OFFSET;
        memset(drl, 0, (unsigned long)ncols * sizeof(int64_t));
        for (j = 0; j < os; ++j) {
            drl[ds[j]]  = (int64_t)cfs[j];
        }
        for (; j < len; j += UNROLL) {
            drl[ds[j]]    = (int64_t)cfs[j];
            drl[ds[j+1]]  = (int64_t)cfs[j+1];
            drl[ds[j+2]]  = (int64_t)cfs[j+2];
            drl[ds[j+3]]  = (int64_t)cfs[j+3];
        }
        cfs = NULL;
        sc  = npiv[OFFSET];
        free(npiv);
        free(cfs);
        npiv  = reduce_dense_row_by_known_pivots_sparse_ff_32(
                drl, mat, bs, pivs, sc, i, st);
        if (!npiv) {
            mat->tr[i]  = NULL;
            continue;
        }
        /* normalize coefficient array
         * NOTE: this has to be done here, otherwise the reduction may
         * lead to wrong results in a parallel computation since other
         * threads might directly use the new pivot once it is synced. */
        /* if (mat->cf_32[npiv[COEFFS]][0] != 1) {
         *     normalize_sparse_matrix_row_ff_32(
         *             mat->cf_32[npiv[COEFFS]], npiv[PRELOOP], npiv[LENGTH], st->fc);
         * } */
        mat->tr[i]  = npiv;
        /* k   = __sync_bool_c#csompare_and_swap(&pivs[npiv[OFFSET]], NULL, npiv); */
        cfs = mat->cf_32[npiv[COEFFS]];
    }

    /* we do not need the old pivots anymore */
    for (i = 0; i < ncl; ++i) {
        free(pivs[i]);
        pivs[i] = NULL;
    }

    free(pivs);
    pivs  = NULL;
    free(dr);
    dr  = NULL;

    mat->np = mat->nr = mat->sz = nrl;
}

static void exact_trace_sparse_reduced_echelon_form_ff_32(
        trace_t *trace,
        mat_t *mat,
        const bs_t * const bs,
        stat_t *st
        )
{
    len_t i = 0, j, k;
    hi_t sc = 0;    /* starting column */

    const len_t ncols = mat->nc;
    const len_t nrl   = mat->nrl;
    const len_t ncr   = mat->ncr;
    const len_t ncl   = mat->ncl;

    /* we fill in all known lead terms in pivs */
    hm_t **pivs   = (hm_t **)calloc((unsigned long)ncols, sizeof(hm_t *));
    memcpy(pivs, mat->rr, (unsigned long)mat->nru * sizeof(hm_t *));

    /* unkown pivot rows we have to reduce with the known pivots first */
    hm_t **upivs  = mat->tr;

    int64_t *dr  = (int64_t *)malloc(
            (unsigned long)(st->nthrds * ncols) * sizeof(int64_t));
    /* mo need to have any sharing dependencies on parallel computation,
     * no data to be synchronized at this step of the linear algebra */
#pragma omp parallel for num_threads(st->nthrds) \
    private(i, j, k, sc) \
    schedule(dynamic)
    for (i = 0; i < nrl; ++i) {
        int64_t *drl  = dr + (omp_get_thread_num() * ncols);
        hm_t *npiv      = upivs[i];
        rba_t *rba      = mat->rba[i];
        cf32_t *cfs     = bs->cf_32[npiv[COEFFS]];
        const len_t bi  = npiv[BINDEX];
        const len_t mh  = npiv[MULT];
        const len_t os  = npiv[PRELOOP];
        const len_t len = npiv[LENGTH];
        const hm_t * const ds = npiv + OFFSET;
        k = 0;
        memset(drl, 0, (unsigned long)ncols * sizeof(int64_t));
        for (j = 0; j < os; ++j) {
            drl[ds[j]]  = (int64_t)cfs[j];
        }
        for (; j < len; j += UNROLL) {
            drl[ds[j]]    = (int64_t)cfs[j];
            drl[ds[j+1]]  = (int64_t)cfs[j+1];
            drl[ds[j+2]]  = (int64_t)cfs[j+2];
            drl[ds[j+3]]  = (int64_t)cfs[j+3];
        }
        cfs = NULL;
        do {
            sc  = npiv[OFFSET];
            free(npiv);
            free(cfs);
            npiv  = mat->tr[i]  = trace_reduce_dense_row_by_known_pivots_sparse_ff_32(
                    rba, drl, mat, bs, pivs, sc, i, mh, bi, st);
            if (!npiv) {
                break;
            }
            /* normalize coefficient array
             * NOTE: this has to be done here, otherwise the reduction may
             * lead to wrong results in a parallel computation since other
             * threads might directly use the new pivot once it is synced. */
            if (mat->cf_32[npiv[COEFFS]][0] != 1) {
                normalize_sparse_matrix_row_ff_32(
                        mat->cf_32[npiv[COEFFS]], npiv[PRELOOP], npiv[LENGTH], st->fc);
                st->trace_nr_mult  +=  npiv[LENGTH] / 1000.0;
            }
            k   = __sync_bool_compare_and_swap(&pivs[npiv[OFFSET]], NULL, npiv);
            cfs = mat->cf_32[npiv[COEFFS]];
        } while (!k);
    }

    /* construct the trace */
    construct_trace(trace, mat);

    /* we do not need the old pivots anymore */
    for (i = 0; i < ncl; ++i) {
        free(pivs[i]);
        pivs[i] = NULL;
    }

    len_t npivs = 0; /* number of new pivots */

    dr      = realloc(dr, (unsigned long)ncols * sizeof(int64_t));
    mat->tr = realloc(mat->tr, (unsigned long)ncr * sizeof(hm_t *));

    /* interreduce new pivots */
    cf32_t *cfs;
    hm_t cf_array_pos;
    for (i = 0; i < ncr; ++i) {
        k = ncols-1-i;
        if (pivs[k]) {
            memset(dr, 0, (unsigned long)ncols * sizeof(int64_t));
            cfs = mat->cf_32[pivs[k][COEFFS]];
            cf_array_pos    = pivs[k][COEFFS];
            const len_t os  = pivs[k][PRELOOP];
            const len_t len = pivs[k][LENGTH];
            const hm_t * const ds = pivs[k] + OFFSET;
            sc  = ds[0];
            for (j = 0; j < os; ++j) {
                dr[ds[j]] = (int64_t)cfs[j];
            }
            for (; j < len; j += UNROLL) {
                dr[ds[j]]    = (int64_t)cfs[j];
                dr[ds[j+1]]  = (int64_t)cfs[j+1];
                dr[ds[j+2]]  = (int64_t)cfs[j+2];
                dr[ds[j+3]]  = (int64_t)cfs[j+3];
            }
            free(pivs[k]);
            free(cfs);
            pivs[k] = NULL;
            pivs[k] = mat->tr[npivs++] =
                reduce_dense_row_by_known_pivots_sparse_ff_32(
                        dr, mat, bs, pivs, sc, cf_array_pos, st);
        }
    }
    free(pivs);
    pivs  = NULL;
    free(dr);
    dr  = NULL;

    mat->tr = realloc(mat->tr, (unsigned long)npivs * sizeof(hi_t *));
    mat->np = mat->nr = mat->sz = npivs;
}

static int exact_application_sparse_reduced_echelon_form_ff_32(
        mat_t *mat,
        const bs_t * const bs,
        stat_t *st
        )
{
    len_t i = 0, j, k;
    hi_t sc = 0;    /* starting column */

    const len_t ncols = mat->nc;
    const len_t nrl   = mat->nrl;
    const len_t ncr   = mat->ncr;
    const len_t ncl   = mat->ncl;

    /* we fill in all known lead terms in pivs */
    hm_t **pivs   = (hm_t **)calloc((unsigned long)ncols, sizeof(hm_t *));
    memcpy(pivs, mat->rr, (unsigned long)mat->nru * sizeof(hm_t *));

    /* unkown pivot rows we have to reduce with the known pivots first */
    hm_t **upivs  = mat->tr;

    int64_t *dr  = (int64_t *)malloc(
            (unsigned long)(st->nthrds * ncols) * sizeof(int64_t));
    /* mo need to have any sharing dependencies on parallel computation,
     * no data to be synchronized at this step of the linear algebra */
    int flag  = 1;
#pragma omp parallel for num_threads(st->nthrds) \
    private(i, j, k, sc) \
    schedule(dynamic)
    for (i = 0; i < nrl; ++i) {
        if (flag == 1) {
            int64_t *drl    = dr + (omp_get_thread_num() * ncols);
            hm_t *npiv      = upivs[i];
            cf32_t *cfs     = bs->cf_32[npiv[COEFFS]];
            const len_t os  = npiv[PRELOOP];
            const len_t len = npiv[LENGTH];
            const hm_t * const ds = npiv + OFFSET;
            k = 0;
            memset(drl, 0, (unsigned long)ncols * sizeof(int64_t));
            for (j = 0; j < os; ++j) {
                drl[ds[j]]  = (int64_t)cfs[j];
            }
            for (; j < len; j += UNROLL) {
                drl[ds[j]]    = (int64_t)cfs[j];
                drl[ds[j+1]]  = (int64_t)cfs[j+1];
                drl[ds[j+2]]  = (int64_t)cfs[j+2];
                drl[ds[j+3]]  = (int64_t)cfs[j+3];
            }
            cfs = NULL;
            do {
                sc  = npiv[OFFSET];
                free(npiv);
                free(cfs);
                npiv  = mat->tr[i]  = reduce_dense_row_by_known_pivots_sparse_ff_32(
                        drl, mat, bs, pivs, sc, i, st);
                if (!npiv) {
                    fprintf(stderr, "Unlucky prime detected, row reduced to zero.");
                    flag  = 0;
                }

                /* normalize coefficient array
                 * NOTE: this has to be done here, otherwise the reduction may
                 * lead to wrong results in a parallel computation since other
                 * threads might directly use the new pivot once it is synced. */
                if (mat->cf_32[npiv[COEFFS]][0] != 1) {
                    normalize_sparse_matrix_row_ff_32(
                            mat->cf_32[npiv[COEFFS]], npiv[PRELOOP], npiv[LENGTH], st->fc);
                    st->application_nr_mult  +=  npiv[LENGTH] / 1000.0;
                }
                k   = __sync_bool_compare_and_swap(&pivs[npiv[OFFSET]], NULL, npiv);
                cfs = mat->cf_32[npiv[COEFFS]];
            } while (!k);
        }
    }
    /* unlucky prime found */
    if (flag == 0) {
        return 1;
    }
    /* we do not need the old pivots anymore */
    for (i = 0; i < ncl; ++i) {
        free(pivs[i]);
        pivs[i] = NULL;
    }

    len_t npivs = 0; /* number of new pivots */

    dr      = realloc(dr, (unsigned long)ncols * sizeof(int64_t));
    mat->tr = realloc(mat->tr, (unsigned long)ncr * sizeof(hm_t *));

    /* interreduce new pivots */
    cf32_t *cfs;
    hm_t cf_array_pos;
    for (i = 0; i < ncr; ++i) {
        k = ncols-1-i;
        if (pivs[k]) {
            memset(dr, 0, (unsigned long)ncols * sizeof(int64_t));
            cfs = mat->cf_32[pivs[k][COEFFS]];
            cf_array_pos    = pivs[k][COEFFS];
            const len_t os  = pivs[k][PRELOOP];
            const len_t len = pivs[k][LENGTH];
            const hm_t * const ds = pivs[k] + OFFSET;
            sc  = ds[0];
            for (j = 0; j < os; ++j) {
                dr[ds[j]] = (int64_t)cfs[j];
            }
            for (; j < len; j += UNROLL) {
                dr[ds[j]]    = (int64_t)cfs[j];
                dr[ds[j+1]]  = (int64_t)cfs[j+1];
                dr[ds[j+2]]  = (int64_t)cfs[j+2];
                dr[ds[j+3]]  = (int64_t)cfs[j+3];
            }
            free(pivs[k]);
            free(cfs);
            pivs[k] = NULL;
            pivs[k] = mat->tr[npivs++] =
                reduce_dense_row_by_known_pivots_sparse_ff_32(
                        dr, mat, bs, pivs, sc, cf_array_pos, st);
        }
    }
    free(pivs);
    pivs  = NULL;
    free(dr);
    dr  = NULL;

    mat->tr = realloc(mat->tr, (unsigned long)npivs * sizeof(hi_t *));
    mat->np = mat->nr = mat->sz = npivs;

    return 0;
}

static cf32_t **sparse_AB_CD_linear_algebra_ff_32(
        mat_t *mat,
        const bs_t * bs,
        const stat_t * const st
        )
{
    len_t i = 0, j;
    hi_t sc = 0;    /* starting column */

    const len_t ncols = mat->nc;
    const len_t nrl   = mat->nrl;
    const len_t ncl   = mat->ncl;

    /* we fill in all known lead terms in pivs */
    hm_t **pivs   = (hm_t **)calloc((unsigned long)ncols, sizeof(hm_t *));
    memcpy(pivs, mat->rr, (unsigned long)mat->nru * sizeof(hm_t *));

    /* unkown pivot rows we have to reduce with the known pivots first */
    hm_t **upivs  = mat->tr;

    /* dense rows representing updated D part;
     * after reducing CD part with AB */
    cf32_t **drs    = (cf32_t **)calloc((unsigned long)nrl, sizeof(cf32_t *));

    int64_t *dr  = (int64_t *)malloc(
            (unsigned long)(st->nthrds * ncols) * sizeof(int64_t));
    /* mo need to have any sharing dependencies on parallel computation,
     * no data to be synchronized at this step of the linear algebra */
#pragma omp parallel for num_threads(st->nthrds) \
    private(i, j, sc) \
    schedule(dynamic)
    for (i = 0; i < nrl; ++i) {
        cf32_t *cfs = NULL;
        int64_t *drl  = dr + (omp_get_thread_num() * ncols);
        hm_t *npiv    = upivs[i];
        /* do the reduction */
        memset(drl, 0, (unsigned long)ncols * sizeof(int64_t));
        cfs = bs->cf_32[npiv[COEFFS]];
        const len_t os  = npiv[PRELOOP];
        const len_t len = npiv[LENGTH];
        const hm_t * const ds = npiv + OFFSET;
        for (j = 0; j < os; ++j) {
            drl[ds[j]]  = (int64_t)cfs[j];
        }
        for (; j < len; j += UNROLL) {
            drl[ds[j]]    = (int64_t)cfs[j];
            drl[ds[j+1]]  = (int64_t)cfs[j+1];
            drl[ds[j+2]]  = (int64_t)cfs[j+2];
            drl[ds[j+3]]  = (int64_t)cfs[j+3];
        }
        sc  = ds[0];
        free(npiv);
        drs[i]  = reduce_dense_row_by_old_pivots_ff_32(
                drl, mat, bs, pivs, sc, st->fc);
    }
    free(dr);
    dr  = NULL;

    /* we do not need the old pivots anymore */
    for (i = 0; i < ncl; ++i) {
        free(pivs[i]);
        pivs[i] = NULL;
    }
    free(pivs);
    pivs  = NULL;

    /* remove NULL dense rows */
    len_t npivs = 0; /* number of new pivots */
    for (i = 0; i < nrl; ++i) {
        if (drs[i] != NULL) {
            drs[npivs++]  = drs[i];
        }
    }
    if (npivs == 0) {
        free(drs);
        drs = NULL;
    }
    mat->np = npivs;

    return drs;
}

static cf32_t **interreduce_dense_matrix_ff_32(
    cf32_t **dm,
    const len_t ncr,
    const uint32_t fc
    )
{
    len_t i, j, k, l;
    int64_t *dr = malloc((unsigned long)ncr * sizeof(int64_t));

    for (i = 0; i < ncr; ++i) {
        k = ncr-1-i;
        if (dm[k]) {
            memset(dr, 0, (unsigned long)ncr * sizeof(int64_t));
            const len_t npc  = ncr - k;
            const len_t os   = npc % UNROLL;
            for (j = k, l = 0; l < os; ++j, ++l) {
                dr[j] = (int64_t)dm[k][l];
            }
            for (; l < npc; j += UNROLL, l += UNROLL) {
                dr[j]   = (int64_t)dm[k][l];
                dr[j+1] = (int64_t)dm[k][l+1];
                dr[j+2] = (int64_t)dm[k][l+2];
                dr[j+3] = (int64_t)dm[k][l+3];
            }
            free(dm[k]);
            dm[k] = NULL;
            /* start with previous pivot the reduction process, so keep the
             * pivot element as it is */
            dm[k] = reduce_dense_row_by_dense_new_pivots_ff_32(
                    dr, &k, dm, ncr, fc);
        }
    }
    free(dr);
    return dm;
}

static cf32_t **exact_dense_linear_algebra_ff_32(
        cf32_t **dm,
        mat_t *mat,
        const stat_t * const st
        )
{
    len_t i, j, k, l, npivs;

    const len_t nrows = mat->np; /* we need the pivots until now here */
    const len_t ncr   = mat->ncr;

    /* rows already representing new pivots */
    cf32_t **nps  = (cf32_t **)calloc((unsigned long)ncr, sizeof(cf32_t *));
    /* rows to be further reduced */
    cf32_t **tbr  = (cf32_t **)calloc((unsigned long)nrows, sizeof(cf32_t *));
    int64_t *dr   = (int64_t *)malloc(
            (unsigned long)(st->nthrds * ncr) * sizeof(int64_t));

    /* separate rows already representing new pivots and rows to
     * be further reduced by these new pivots */
    j     = 0;
    npivs = 0;
    for (i = 0; i < nrows; ++i) {
        if (dm[i] != NULL) {
            k = 0;
            while (dm[i][k] == 0) {
                ++k;
            }
            if (nps[k] == NULL) {
                /* we have a pivot, cut the dense row down to start
                 * at the first nonzero entry */
                memmove(dm[i], dm[i]+k, (unsigned long)(ncr-k) * sizeof(cf32_t));
                dm[i] = realloc(dm[i], (unsigned long)(ncr-k) * sizeof(cf32_t));
                nps[k] = dm[i];
                if (nps[k][0] != 1) {
                    nps[k]  = normalize_dense_matrix_row_ff_32(nps[k], ncr-k, st->fc);
                }
            } else {
                tbr[j++]  = dm[i];
            }
        }
    }
    free(dm);
    dm  = NULL;

    const len_t ntr = j;
    tbr = realloc(tbr, (unsigned long)ntr * sizeof(cf32_t *));
    /* offset modulo 4 for loop unrolling, +1 due to storing the first
     * nonzero entry at the first position */

    /* reduction process to get all possible pivots, no interreduction here */
#pragma omp parallel for num_threads(st->nthrds) \
    private(i, j, k, l) shared(nps, tbr) \
    schedule(dynamic)
    for (i = 0; i < ntr; ++i) {
        int64_t *drl  = dr + (omp_get_thread_num() * ncr);
        memset(drl, 0, (unsigned long)ncr * sizeof(int64_t));
        hm_t npc  = 0;
        hm_t os   = 0;
        cf32_t *npiv  = tbr[i];
        os   = (ncr-npc) % UNROLL;
        for (l = 0, j = npc; l < os; ++l, ++j) {
            drl[j]  = (int64_t)npiv[l];
        }
        for (; j < ncr; l += UNROLL, j += UNROLL) {
            drl[j]    = (int64_t)npiv[l];
            drl[j+1]  = (int64_t)npiv[l+1];
            drl[j+2]  = (int64_t)npiv[l+2];
            drl[j+3]  = (int64_t)npiv[l+3];
        }
        do {
            free(npiv);
            npiv = NULL;
            npiv = reduce_dense_row_by_dense_new_pivots_ff_32(
                    drl, &npc, nps, mat->ncr, st->fc);
            if (npc == -1) {
                break;
            }
            k = __sync_bool_compare_and_swap(&nps[npc], NULL, npiv);
            /* some other thread has already added a pivot so we have to
             * recall the dense reduction process */
        } while (!k);
    }
    /* count number of pivots */
    const len_t os  = ncr % UNROLL;
    for (i = 0; i < os; ++i) {
        if (nps[i] != NULL) {
            npivs++;
        }
    }
    for (; i < ncr; i += UNROLL) {
        if (nps[i] != NULL) {
            npivs++;
        }
        if (nps[i+1] != NULL) {
            npivs++;
        }
        if (nps[i+2] != NULL) {
            npivs++;
        }
        if (nps[i+3] != NULL) {
            npivs++;
        }
    }
    mat->np = npivs;

    free(tbr);
    free(dr);

    return nps;
}

static cf32_t **probabilistic_dense_linear_algebra_ff_32(
        cf32_t **dm,
        mat_t *mat,
        const stat_t * const st
        )
{
    len_t i, j, k, l, m, npivs;

    const uint32_t fc = st->fc;
    const len_t nrows = mat->np; /* we need the pivots until now here */
    const len_t ncols = mat->nc;
    const len_t ncr   = mat->ncr;

    /* rows already representing new pivots */
    cf32_t **nps  = (cf32_t **)calloc((unsigned long)ncr, sizeof(cf32_t *));
    /* rows to be further reduced */
    cf32_t **tbr  = (cf32_t **)calloc((unsigned long)nrows, sizeof(cf32_t *));

    /* separate rows already representing new pivots and rows to
     * be further reduced by these new pivots */
    j     = 0;
    npivs = 0;
    for (i = 0; i < nrows; ++i) {
        if (dm[i] != NULL) {
            k = 0;
            while (dm[i][k] == 0) {
                ++k;
            }
            if (nps[k] == NULL) {
                /* we have a pivot, cut the dense row down to start
                 * at the first nonzero entry */
                memmove(dm[i], dm[i]+k, (unsigned long)(ncr-k) * sizeof(cf32_t));
                dm[i] = realloc(dm[i], (unsigned long)(ncr-k) * sizeof(cf32_t));
                nps[k] = dm[i];
                if (nps[k][0] != 1) {
                    nps[k]  = normalize_dense_matrix_row_ff_32(nps[k], ncr-k, st->fc);
                }
            } else {
                tbr[j++]  = dm[i];
            }
        }
    }
    free(dm);
    dm  = NULL;

    const len_t ntr = j;
    tbr = realloc(tbr, (unsigned long)ntr * sizeof(cf32_t *));

    /* Why do we generate the random linear combinations so strangely
     * compared to, e.g. la_ff_8.c or la_ff_16.c?
     * We can have fc > 2^31, so shifting the result of random multiplier
     * and coefficient may lead to elements of size > 2^63 which cannot
     * be represented by int64_t anymore. Thus we restrict the multiplier
     * via masking with mask to < 2^31. Then, after subtracting the
     * multiplied values we have to check if at some point the result gets
     * negative as int64_t, i.e. if drl[i] >> 63 is 1. If so, we add a
     * corresponding multiple of fc. Again, if fc > 2^31 we cannot just
     * add fc * fc, but we have to restrict to fc/2 * fc.
     * Note: We cannot apply this trick to the reduction itself, there we
     * cannot restrict the multiplier to be < 2^31, so we have to handle the
     * fc > 2^31 case differently. */
    uint64_t tmp  = (uint64_t)fc * fc;
    while (tmp > pow(2,63)) {
      tmp -=  (uint64_t)(fc/2) * fc;
    }
    const int64_t mod2  = tmp;

    /* compute rows per block */
    const len_t nb  = (len_t)(floor(sqrt(ntr/3)))+1;
    const len_t rem = (ntr % nb == 0) ? 0 : 1;
    const len_t rpb = (ntr / nb) + rem;
    const int64_t mask  = pow(2,(uint32_t)(ceil(log((double)st->max_uht_size)/log(2))))-1;

    int64_t *dr   = (int64_t *)malloc(
        (unsigned long)(st->nthrds * ncols) * sizeof(int64_t));
    int64_t *mul  = (int64_t *)malloc(
        (unsigned long)(st->nthrds * rpb) * sizeof(int64_t));

    /* reduction process to get all possible pivots, no interreduction here */
#pragma omp parallel for num_threads(st->nthrds) \
    private(i, j, k, l) shared(nps, tbr) \
    schedule(dynamic)
    for (i = 0; i < ntr; ++i) {
        int64_t *drl  = dr + (omp_get_thread_num() * ncr);
        int64_t *mull = mul + (omp_get_thread_num() * rpb);
        const int32_t nbl   = (int32_t) (ntr > (i+1)*rpb ? (i+1)*rpb : ntr);
        const int32_t nrbl  = (int32_t) (nbl - i*rpb);
        
        if (nrbl > 0) {
            hm_t npc;
            hm_t os;
            cf32_t *tmp;
            len_t bctr  = 0;
            while (bctr < nrbl) {
                npc = 0;
                os  = ncr % UNROLL;

                /* fill random value array */
                for (j = 0; j < nrbl; ++j) {
                    mull[j] = (int64_t)rand() & mask;
                }
                /* generate one dense row as random linear combination
                 * of the rows of the block */
                memset(drl, 0, (unsigned long)ncr * sizeof(int64_t));

                for (k = 0, m = i*rpb; m < nbl; ++k, ++m) {
                    tmp  = tbr[m];
                    for (l = 0, j = npc; l < os; ++l, ++j) {
                        drl[j]  -=  mull[k] * tmp[l];
                        drl[j]  +=  (drl[j] >> 63) & mod2;
                    }
                    for (; j < ncr; l += UNROLL, j += UNROLL) {
                        drl[j]    -=  mull[k] * tmp[l];
                        drl[j]    +=  (drl[j] >> 63) & mod2;
                        drl[j+1]  -=  mull[k] * tmp[l+1];
                        drl[j+1]  +=  (drl[j+1] >> 63) & mod2;
                        drl[j+2]  -=  mull[k] * tmp[l+2];
                        drl[j+2]  +=  (drl[j+2] >> 63) & mod2;
                        drl[j+3]  -=  mull[k] * tmp[l+3];
                        drl[j+3]  +=  (drl[j+3] >> 63) & mod2;
                    }
                }
                k   = 0;
                npc = 0;
                /* do the reduction */
                tmp = NULL;
                do {
                    free(tmp);
                    tmp = reduce_dense_row_by_dense_new_pivots_ff_32(
                            drl, &npc, nps, mat->ncr, st->fc);
                    if (npc == -1) {
                        bctr  = nrbl;
                        break;
                    }
                    k = __sync_bool_compare_and_swap(&nps[npc], NULL, tmp);
                    /* some other thread has already added a pivot so we have to
                    * recall the dense reduction process */
                } while (!k);
                bctr++;
            }
            for (j = i*rpb; j < nbl; ++j) {
                free(tbr[j]);
                tbr[j]  = NULL;
            }
        }
    }
    /* count number of pivots */
    const len_t os  = ncr % UNROLL;
    for (i = 0; i < os; ++i) {
        if (nps[i] != NULL) {
            npivs++;
        }
    }
    for (; i < ncr; i += UNROLL) {
        if (nps[i] != NULL) {
            npivs++;
        }
        if (nps[i+1] != NULL) {
            npivs++;
        }
        if (nps[i+2] != NULL) {
            npivs++;
        }
        if (nps[i+3] != NULL) {
            npivs++;
        }
    }
    mat->np = npivs;

    free(mul);
    free(tbr);
    free(dr);

    return nps;
}

static cf32_t **probabilistic_sparse_dense_echelon_form_ff_32(
        mat_t *mat,
        const bs_t * const bs,
        const stat_t * const st
        )
{
    len_t i = 0, j, k, l, m, npivs;

    const len_t nru   = mat->nru;
    const len_t nrl   = mat->nrl;
    const len_t ncr   = mat->ncr;
    const len_t ncols = mat->nc;

    /* we fill in all known lead terms in pivs */
    hm_t **pivs   = (hm_t **)calloc((unsigned long)ncols, sizeof(hm_t *));
    memcpy(pivs, mat->rr, (unsigned long)mat->nru * sizeof(hm_t *));

    /* unkown pivot rows we have to reduce with the known pivots first */
    hm_t **upivs  = mat->tr;

    /* rows already representing new pivots */
    cf32_t **nps  = (cf32_t **)calloc((unsigned long)ncr, sizeof(cf32_t *));

    const uint32_t fc   = st->fc;
    const int64_t mod2  = (int64_t)fc * fc;

    /* compute rows per block */
    const len_t nb  = (len_t)(floor(sqrt(nrl/3)))+1;
    const len_t rem = (nrl % nb == 0) ? 0 : 1;
    const len_t rpb = (nrl / nb) + rem;

    int64_t *dr   = (int64_t *)malloc(
        (unsigned long)(st->nthrds * ncols) * sizeof(int64_t));
    int64_t *mul  = (int64_t *)malloc(
        (unsigned long)(st->nthrds * rpb) * sizeof(int64_t));

    /* reduction process to get all possible pivots, no interreduction here */
#pragma omp parallel for num_threads(st->nthrds) \
    private(i, j, k, l, m) shared(nps) \
    schedule(dynamic)
    for (i = 0; i < nb; ++i) {
        int64_t *drl  = dr + (omp_get_thread_num() * ncols);
        int64_t *mull = mul + (omp_get_thread_num() * rpb);
        const int32_t nbl   = (int32_t) (nrl > (i+1)*rpb ? (i+1)*rpb : nrl);
        const int32_t nrbl  = (int32_t) (nbl - i*rpb);
        if (nrbl > 0) {
            hm_t *npiv;
            cf32_t *tmp;
            hm_t npc;
            len_t bctr  = 0;
            while (bctr < nrbl) {
                npc = 0;

                /* fill random value array */
                for (j = 0; j < nrbl; ++j) {
                    mull[j] = (int64_t)rand() % fc;
                }
                /* generate one dense row as random linear combination
                 * of the rows of the block */
                memset(drl, 0, (unsigned long)ncols * sizeof(int64_t));

                for (k = 0, m = i*rpb; m < nbl; ++k, ++m) {
                    npiv  = upivs[m];
                    tmp   = bs->cf_32[npiv[COEFFS]];
                    const len_t os  = npiv[PRELOOP];
                    const len_t len = npiv[LENGTH];
                    const hm_t * const ds = npiv + OFFSET;
                    for (l = 0; l < os; ++l) {
                        drl[ds[l]]  -=  mull[k] * tmp[l];
                        drl[ds[l]]  +=  (drl[ds[l]] >> 63) & mod2;
                    }
                    for (; l < len; l += UNROLL) {
                        drl[ds[l]]    -=  mull[k] * tmp[l];
                        drl[ds[l]]    +=  (drl[ds[l]] >> 63) & mod2;
                        drl[ds[l+1]]  -=  mull[k] * tmp[l+1];
                        drl[ds[l+1]]  +=  (drl[ds[l+1]] >> 63) & mod2;
                        drl[ds[l+2]]  -=  mull[k] * tmp[l+2];
                        drl[ds[l+2]]  +=  (drl[ds[l+2]] >> 63) & mod2;
                        drl[ds[l+3]]  -=  mull[k] * tmp[l+3];
                        drl[ds[l+3]]  +=  (drl[ds[l+3]] >> 63) & mod2;
                    }
                }
                k   = 0;
                npc = 0;
                /* do the reduction */
                tmp = NULL;
                do {
                    free(tmp);
                    tmp = reduce_dense_row_by_all_pivots_ff_32(
                            drl, mat, bs, &npc, pivs, nps, st->fc);
                    if (npc == -1) {
                        bctr  = nrbl;
                        break;
                    }
                    k = __sync_bool_compare_and_swap(&nps[npc], NULL, tmp);
                    /* some other thread has already added a pivot so we have to
                    * recall the dense reduction process */
                } while (!k);
                bctr++;
            }
            for (j = i*rpb; j < nbl; ++j) {
                free(upivs[j]);
                upivs[j]  = NULL;
            }
        }
    }
    npivs = 0;
    /* count number of pivots */
    const len_t os  = ncr % UNROLL;
    for (i = 0; i < os; ++i) {
        if (nps[i] != NULL) {
            npivs++;
        }
    }
    for (; i < ncr; i += UNROLL) {
        if (nps[i] != NULL) {
            npivs++;
        }
        if (nps[i+1] != NULL) {
            npivs++;
        }
        if (nps[i+2] != NULL) {
            npivs++;
        }
        if (nps[i+3] != NULL) {
            npivs++;
        }
    }
    mat->np = npivs;


    for (i = 0; i < nru; ++i) {
        free(pivs[i]);
    }
    free(pivs);
    pivs  = NULL;
    free(mul);
    mul = NULL;
    free(dr);
    dr  = NULL;

    return nps;
}

static void convert_to_sparse_matrix_rows_ff_32(
        mat_t *mat,
        cf32_t * const * const dm
        )
{
    if (mat->np == 0) {
        return;
    }

    len_t i, j, k, l, m;
    cf32_t *cfs;
    hm_t *dts, *dss;

    const len_t ncr = mat->ncr;
    const len_t ncl = mat->ncl;

    mat->tr     = realloc(mat->tr, (unsigned long)mat->np * sizeof(hm_t *));
    mat->cf_32  = realloc(mat->cf_32,
            (unsigned long)mat->np * sizeof(cf32_t *));

    l = 0;
    for (i = 0; i < ncr; ++i) {
        m = ncr-1-i;
        if (dm[m] != NULL) {
            cfs = malloc((unsigned long)(ncr-m) * sizeof(cf32_t));
            dts = malloc((unsigned long)(ncr-m+OFFSET) * sizeof(hm_t));
            const hm_t len    = ncr-m;
            const hm_t os     = len % UNROLL;
            const hm_t shift  = ncl+m;
            dss = dts + OFFSET;

            for (k = 0, j = 0; j < os; ++j) {
                if (dm[m][j] != 0) {
                    cfs[k]    = dm[m][j];
                    dss[k++]  = j+shift;
                }
            }
            for (; j < len; j += UNROLL) {
                if (dm[m][j] != 0) {
                    cfs[k]    = dm[m][j];
                    dss[k++]  = j+shift;
                }
                if (dm[m][j+1] != 0) {
                    cfs[k]    = dm[m][j+1];
                    dss[k++]  = j+1+shift;
                }
                if (dm[m][j+2] != 0) {
                    cfs[k]    = dm[m][j+2];
                    dss[k++]  = j+2+shift;
                }
                if (dm[m][j+3] != 0) {
                    cfs[k]    = dm[m][j+3];
                    dss[k++]  = j+3+shift;
                }
            }

            /* store meta data in first entries */
            dts[COEFFS]   = l; /* position of coefficient array in tmpcf */
            dts[PRELOOP]  = k % UNROLL;
            dts[LENGTH]   = k;

            /* adjust memory usage */
            dts = realloc(dts, (unsigned long)(k+OFFSET) * sizeof(hm_t));
            cfs = realloc(cfs, (unsigned long)k * sizeof(cf32_t));

            /* link to basis */
            mat->tr[l]    = dts;
            mat->cf_32[l] = cfs;
            l++;
        }
    }
}

/* NOTE: this note is about the different linear algebra implementations:
 * exact and probabilistic linear algebra differ only in the last,
 * dense reduction step: the reduction of CD via AB is sparse and
 * the same for both. this generates a dense D' part which is then
 * either reduced via exact linear algebra or via probabilistic
 * linear algebra */
static void probabilistic_sparse_linear_algebra_ff_32(
        mat_t *mat,
        const bs_t * const bs,
        stat_t *st
        )
{
    /* timings */
    double ct0, ct1, rt0, rt1;
    ct0 = cputime();
    rt0 = realtime();

    /* allocate temporary storage space for sparse
     * coefficients of new pivot rows */
    mat->cf_32 = realloc(mat->cf_32,
            (unsigned long)mat->nrl * sizeof(cf32_t *));
    probabilistic_sparse_reduced_echelon_form_ff_32(mat, bs, st);

    /* timings */
    ct1 = cputime();
    rt1 = realtime();
    st->la_ctime  +=  ct1 - ct0;
    st->la_rtime  +=  rt1 - rt0;

    st->num_zerored += (mat->nrl - mat->np);
    if (st->info_level > 1) {
        printf("%7d new %7d zero", mat->np, mat->nrl - mat->np);
        fflush(stdout);
    }
}

static void exact_sparse_linear_algebra_ff_32(
        mat_t *mat,
        const bs_t * const bs,
        stat_t *st
        )
{
    /* timings */
    double ct0, ct1, rt0, rt1;
    ct0 = cputime();
    rt0 = realtime();

    /* allocate temporary storage space for sparse
     * coefficients of new pivot rows */
    mat->cf_32  = realloc(mat->cf_32,
            (unsigned long)mat->nrl * sizeof(cf32_t *));
    exact_sparse_reduced_echelon_form_ff_32(mat, bs, st);

    /* timings */
    ct1 = cputime();
    rt1 = realtime();
    st->la_ctime  +=  ct1 - ct0;
    st->la_rtime  +=  rt1 - rt0;

    st->num_zerored += (mat->nrl - mat->np);
    if (st->info_level > 1) {
        printf("%7d new %7d zero", mat->np, mat->nrl - mat->np);
        fflush(stdout);
    }
}

static void exact_sparse_linear_algebra_nf_ff_32(
        mat_t *mat,
        const bs_t * const tbr,
        const bs_t * const bs,
        stat_t *st
        )
{
    /* timings */
    double ct0, ct1, rt0, rt1;
    ct0 = cputime();
    rt0 = realtime();

    len_t i;

    /* allocate temporary storage space for sparse
     * coefficients of new pivot rows */
    mat->cf_32  = realloc(mat->cf_32,
            (unsigned long)mat->nrl * sizeof(cf32_t *));
    exact_sparse_reduced_echelon_form_nf_ff_32(mat, tbr, bs, st);

    /* timings */
    ct1 = cputime();
    rt1 = realtime();
    st->la_ctime  +=  ct1 - ct0;
    st->la_rtime  +=  rt1 - rt0;

    st->num_zerored += (mat->nrl - mat->np);
    uint32_t zeroes = 0;
    for (i = 0; i < mat->nrl; ++i) {
        if (mat->tr[i] == NULL) {
            zeroes  +=  1;
        }
    }
    if (st->info_level > 1) {
        printf("%7d new w/ %4d zero", mat->np, zeroes);
        fflush(stdout);
    }
}

static int exact_application_sparse_linear_algebra_ff_32(
        mat_t *mat,
        const bs_t * const bs,
        stat_t *st
        )
{
    /* timings */
    double ct0, ct1, rt0, rt1;
    ct0 = cputime();
    rt0 = realtime();

    int ret;

    /* allocate temporary storage space for sparse
     * coefficients of new pivot rows */
    mat->cf_32 = realloc(mat->cf_32,
            (unsigned long)mat->nrl * sizeof(cf32_t *));
    ret = exact_application_sparse_reduced_echelon_form_ff_32(mat, bs, st);

    /* timings */
    ct1 = cputime();
    rt1 = realtime();
    st->la_ctime  +=  ct1 - ct0;
    st->la_rtime  +=  rt1 - rt0;

    st->num_zerored += (mat->nrl - mat->np);
    if (st->info_level > 1) {
        printf("%7d new %7d zero", mat->np, mat->nrl - mat->np);
        fflush(stdout);
    }

    return ret;
}

static void exact_trace_sparse_linear_algebra_ff_32(
        trace_t *trace,
        mat_t *mat,
        const bs_t * const bs,
        stat_t *st
        )
{
    /* timings */
    double ct0, ct1, rt0, rt1;
    ct0 = cputime();
    rt0 = realtime();

    /* allocate temporary storage space for sparse
     * coefficients of new pivot rows */
    mat->cf_32  = realloc(mat->cf_32,
            (unsigned long)mat->nrl * sizeof(cf32_t *));
    exact_trace_sparse_reduced_echelon_form_ff_32(trace, mat, bs, st);

    /* timings */
    ct1 = cputime();
    rt1 = realtime();
    st->la_ctime  +=  ct1 - ct0;
    st->la_rtime  +=  rt1 - rt0;

    st->num_zerored += (mat->nrl - mat->np);
    if (st->info_level > 1) {
        printf("%7d new %7d zero", mat->np, mat->nrl - mat->np);
        fflush(stdout);
    }
}

static void exact_sparse_dense_linear_algebra_ff_32(
        mat_t *mat,
        const bs_t * const bs,
        stat_t *st
        )
{
    len_t i;

    /* timings */
    double ct0, ct1, rt0, rt1;
    ct0 = cputime();
    rt0 = realtime();

    const len_t ncr = mat->ncr;

    /* generate updated dense D part via reduction of CD with AB */
    cf32_t **dm;
    dm  = sparse_AB_CD_linear_algebra_ff_32(mat, bs, st);
    if (mat->np > 0) {      
        dm  = exact_dense_linear_algebra_ff_32(dm, mat, st);
        dm  = interreduce_dense_matrix_ff_32(dm, ncr, st->fc);
    }

    /* convert dense matrix back to sparse matrix representation,
     * use tmpcf for storing the coefficient arrays */
    convert_to_sparse_matrix_rows_ff_32(mat, dm);

    /* free dm */
    if (dm) {
        for (i = 0; i < ncr; ++i) {
            free(dm[i]);
        }
        free(dm);
        dm  = NULL;
    }

    /* timings */
    ct1 = cputime();
    rt1 = realtime();
    st->la_ctime  +=  ct1 - ct0;
    st->la_rtime  +=  rt1 - rt0;

    st->num_zerored += (mat->nrl - mat->np);
    if (st->info_level > 1) {
        printf("%7d new %7d zero", mat->np, mat->nrl - mat->np);
        fflush(stdout);
    }
}

static void probabilistic_sparse_dense_linear_algebra_ff_32_2(
        mat_t *mat,
        const bs_t * const bs,
        stat_t *st
        )
{
    len_t i;

    /* timings */
    double ct0, ct1, rt0, rt1;
    ct0 = cputime();
    rt0 = realtime();

    const len_t ncr = mat->ncr;

    /* generate updated dense D part via reduction of CD with AB */
    cf32_t **dm;
    dm  = sparse_AB_CD_linear_algebra_ff_32(mat, bs, st);
    if (mat->np > 0) {      
        dm  = probabilistic_dense_linear_algebra_ff_32(dm, mat, st);
        dm  = interreduce_dense_matrix_ff_32(dm, mat->ncr, st->fc);
    }

    /* convert dense matrix back to sparse matrix representation,
     * use tmpcf for storing the coefficient arrays */
    convert_to_sparse_matrix_rows_ff_32(mat, dm);

    /* free dm */
    if (dm) {
        for (i = 0; i < ncr; ++i) {
            free(dm[i]);
        }
        free(dm);
        dm  = NULL;
    }

    /* timings */
    ct1 = cputime();
    rt1 = realtime();
    st->la_ctime  +=  ct1 - ct0;
    st->la_rtime  +=  rt1 - rt0;

    st->num_zerored += (mat->nrl - mat->np);
    if (st->info_level > 1) {
        printf("%7d new %7d zero", mat->np, mat->nrl - mat->np);
        fflush(stdout);
    }
}

static void probabilistic_sparse_dense_linear_algebra_ff_32(
        mat_t *mat,
        const bs_t * const bs,
        stat_t *st
        )
{
    len_t i;

    /* timings */
    double ct0, ct1, rt0, rt1;
    ct0 = cputime();
    rt0 = realtime();

    const len_t ncr = mat->ncr;

    /* generate updated dense D part via reduction of CD with AB */
    cf32_t **dm = NULL;
    mat->np = 0;
    dm      = probabilistic_sparse_dense_echelon_form_ff_32(mat, bs, st);
    dm      = interreduce_dense_matrix_ff_32(dm, mat->ncr, st->fc);

    /* convert dense matrix back to sparse matrix representation,
     * use tmpcf for storing the coefficient arrays */
    convert_to_sparse_matrix_rows_ff_32(mat, dm);

    /* free dm */
    if (dm) {
        for (i = 0; i < ncr; ++i) {
            free(dm[i]);
        }
        free(dm);
        dm  = NULL;
    }

    /* timings */
    ct1 = cputime();
    rt1 = realtime();
    st->la_ctime  +=  ct1 - ct0;
    st->la_rtime  +=  rt1 - rt0;

    st->num_zerored += (mat->nrl - mat->np);
    if (st->info_level > 1) {
        printf("%7d new %7d zero", mat->np, mat->nrl - mat->np);
        fflush(stdout);
    }
}

static void interreduce_matrix_rows_ff_32(
        mat_t *mat,
        bs_t *bs,
        stat_t *st
        )
{
    len_t i, j, k, l;

    const len_t nrows = mat->nr;
    const len_t ncols = mat->nc;

    /* adjust displaying timings for statistic printout */
    if (st->info_level > 1) {
        printf("                        ");
    }
    mat->tr = realloc(mat->tr, (unsigned long)ncols * sizeof(hm_t *));

    mat->cf_32  = realloc(mat->cf_32,
            (unsigned long)ncols * sizeof(cf32_t *));
    memset(mat->cf_32, 0, (unsigned long)ncols * sizeof(cf32_t *));
    hm_t **pivs = (hm_t **)calloc((unsigned long)ncols, sizeof(hm_t *));
    /* copy coefficient arrays from basis in matrix, maybe
     * several rows need the same coefficient arrays, but we
     * cannot share them here. */
    for (i = 0; i < nrows; ++i) {
        pivs[mat->rr[i][OFFSET]]  = mat->rr[i];
    }

    int64_t *dr = (int64_t *)malloc((unsigned long)ncols * sizeof(int64_t));
    /* interreduce new pivots */
    cf32_t *cfs;
    /* starting column, coefficient array position in tmpcf */
    hm_t sc;
    k = nrows - 1;
    for (i = 0; i < ncols; ++i) {
        l = ncols-1-i;
        if (pivs[l] != NULL) {
            memset(dr, 0, (unsigned long)ncols * sizeof(int64_t));
            cfs = bs->cf_32[pivs[l][COEFFS]];
            const len_t os  = pivs[l][PRELOOP];
            const len_t len = pivs[l][LENGTH];
            const hm_t * const ds = pivs[l] + OFFSET;
            sc  = ds[0];
            for (j = 0; j < os; ++j) {
                dr[ds[j]] = (int64_t)cfs[j];
            }
            for (; j < len; j += UNROLL) {
                dr[ds[j]]   = (int64_t)cfs[j];
                dr[ds[j+1]] = (int64_t)cfs[j+1];
                dr[ds[j+2]] = (int64_t)cfs[j+2];
                dr[ds[j+3]] = (int64_t)cfs[j+3];
            }
            free(pivs[l]);
            pivs[l] = NULL;
            pivs[l] = mat->tr[k--] =
                reduce_dense_row_by_known_pivots_sparse_ff_32(
                        dr, mat, bs, pivs, sc, l, st);
        }
    }
    /* free now all polynomials in the basis and reset bs->ld to 0. */
    free_basis_elements(bs);
    free(mat->rr);
    mat->rr = NULL;
    mat->np = nrows;
    free(pivs);
    free(dr);
}