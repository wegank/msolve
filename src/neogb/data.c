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

void normalize_initial_basis(
        bs_t *bs,
        const uint32_t fc
        )
{
    if (bs != NULL
            && bs->ht != NULL
            && bs->ht->ops != NULL
            && bs->ht->ops->normalize_initial_basis != NULL) {
        bs->ht->ops->normalize_initial_basis(bs, fc);
        return;
    }
    dispatch_normalize_initial_basis(bs, fc);
}

int initial_input_cmp(
        const void *a,
        const void *b,
        void *htp
        )
{
    ht_t *ht = (ht_t *)htp;
    if (ht != NULL && ht->ops != NULL && ht->ops->initial_input_cmp != NULL) {
        return ht->ops->initial_input_cmp(a, b, htp);
    }
    return dispatch_initial_input_cmp(a, b, htp);
}

int initial_gens_cmp(
        const void *a,
        const void *b,
        void *htp
        )
{
    ht_t *ht = (ht_t *)htp;
    if (ht != NULL && ht->ops != NULL && ht->ops->initial_gens_cmp != NULL) {
        return ht->ops->initial_gens_cmp(a, b, htp);
    }
    return dispatch_initial_gens_cmp(a, b, htp);
}

int monomial_cmp(
        const hi_t a,
        const hi_t b,
        const ht_t *ht
        )
{
    if (ht != NULL && ht->ops != NULL && ht->ops->monomial_cmp != NULL) {
        return ht->ops->monomial_cmp(a, b, ht);
    }
    return dispatch_monomial_cmp(a, b, ht);
}

int spair_cmp(
        const void *a,
        const void *b,
        void *htp
        )
{
    ht_t *ht = (ht_t *)htp;
    if (ht != NULL && ht->ops != NULL && ht->ops->spair_cmp != NULL) {
        return ht->ops->spair_cmp(a, b, htp);
    }
    return dispatch_spair_cmp(a, b, htp);
}

int hcm_cmp(
        const void *a,
        const void *b,
        void *htp
        )
{
    ht_t *ht = (ht_t *)htp;
    if (ht != NULL && ht->ops != NULL && ht->ops->hcm_cmp != NULL) {
        return ht->ops->hcm_cmp(a, b, htp);
    }
    return dispatch_hcm_cmp(a, b, htp);
}

void sba_linear_algebra(
        smat_t *smat,
        crit_t *syz,
        md_t *st,
        const ht_t * const ht
        )
{
    if (st != NULL && st->ops.sba_linear_algebra != NULL) {
        st->ops.sba_linear_algebra(smat, syz, st, ht);
        return;
    }
    dispatch_sba_linear_algebra(smat, syz, st, ht);
}

void exact_linear_algebra(
        mat_t *mat,
        const bs_t * const tbr,
        const bs_t * const bs,
        md_t *st
        )
{
    if (st != NULL && st->ops.exact_linear_algebra != NULL) {
        st->ops.exact_linear_algebra(mat, tbr, bs, st);
        return;
    }
    dispatch_exact_linear_algebra(mat, tbr, bs, st);
}

void linear_algebra(
        mat_t *mat,
        const bs_t * const tbr,
        const bs_t * const bs,
        md_t *st
        )
{
    if (st != NULL && st->ops.linear_algebra != NULL) {
        st->ops.linear_algebra(mat, tbr, bs, st);
        return;
    }
    dispatch_linear_algebra(mat, tbr, bs, st);
}

int application_linear_algebra(
        mat_t *mat,
        const bs_t * const bs,
        md_t *st
        )
{
    if (st != NULL && st->ops.application_linear_algebra != NULL) {
        return st->ops.application_linear_algebra(mat, bs, st);
    }
    return dispatch_application_linear_algebra(mat, bs, st);
}

void trace_linear_algebra(
        trace_t *trace,
        mat_t *mat,
        const bs_t * const bs,
        md_t *st
        )
{
    if (st != NULL && st->ops.trace_linear_algebra != NULL) {
        st->ops.trace_linear_algebra(trace, mat, bs, st);
        return;
    }
    dispatch_trace_linear_algebra(trace, mat, bs, st);
}

void interreduce_matrix_rows(
        mat_t *mat,
        bs_t *bs,
        md_t *st,
        int free_basis
        )
{
    if (st != NULL && st->ops.interreduce_matrix_rows != NULL) {
        st->ops.interreduce_matrix_rows(mat, bs, st, free_basis);
        return;
    }
    dispatch_interreduce_matrix_rows(mat, bs, st, free_basis);
}

cf32_t *reduce_dense_row_by_old_pivots_ff_32(
        int64_t *dr,
        mat_t *mat,
        const bs_t * const bs,
        hm_t * const * const pivs,
        const hi_t dpiv,
        const uint32_t fc
        )
{
    return dispatch_reduce_dense_row_by_old_pivots_ff_32(dr, mat, bs, pivs, dpiv, fc);
}

hm_t *sba_reduce_dense_row_by_known_pivots_sparse_ff_32(
        int64_t *dr,
        smat_t *smat,
        hm_t *const *pivs,
        const hi_t dpiv,
        const hm_t sm,
        const len_t si,
        const len_t ri,
        md_t *st
        )
{
    if (st != NULL && st->ops.sba_reduce_dense_row_by_known_pivots_sparse_ff_32 != NULL) {
        return st->ops.sba_reduce_dense_row_by_known_pivots_sparse_ff_32(
                dr, smat, pivs, dpiv, sm, si, ri, st);
    }
    return dispatch_sba_reduce_dense_row_by_known_pivots_sparse_ff_32(
            dr, smat, pivs, dpiv, sm, si, ri, st);
}

hm_t *reduce_dense_row_by_known_pivots_sparse_ff_32(
        int64_t *dr,
        mat_t *mat,
        const bs_t * const bs,
        hm_t *const *pivs,
        const hi_t dpiv,
        const hm_t tmp_pos,
        const len_t mh,
        const len_t bi,
        const len_t tr,
        md_t *st
        )
{
    if (st != NULL && st->ops.reduce_dense_row_by_known_pivots_sparse_ff_32 != NULL) {
        return st->ops.reduce_dense_row_by_known_pivots_sparse_ff_32(
                dr, mat, bs, pivs, dpiv, tmp_pos, mh, bi, tr, st);
    }
    return dispatch_reduce_dense_row_by_known_pivots_sparse_ff_32(
            dr, mat, bs, pivs, dpiv, tmp_pos, mh, bi, tr, st);
}

hm_t *trace_reduce_dense_row_by_known_pivots_sparse_ff_32(
        rba_t *rba,
        int64_t *dr,
        mat_t *mat,
        const bs_t * const bs,
        hm_t *const *pivs,
        const hi_t dpiv,
        const hm_t tmp_pos,
        const len_t mh,
        const len_t bi,
        md_t *st
        )
{
    if (st != NULL && st->ops.trace_reduce_dense_row_by_known_pivots_sparse_ff_32 != NULL) {
        return st->ops.trace_reduce_dense_row_by_known_pivots_sparse_ff_32(
                rba, dr, mat, bs, pivs, dpiv, tmp_pos, mh, bi, st);
    }
    return dispatch_trace_reduce_dense_row_by_known_pivots_sparse_ff_32(
            rba, dr, mat, bs, pivs, dpiv, tmp_pos, mh, bi, st);
}

cf32_t *reduce_dense_row_by_all_pivots_ff_32(
        int64_t *dr,
        mat_t *mat,
        const bs_t * const bs,
        len_t *pc,
        hm_t *const *pivs,
        cf32_t *const *dpivs,
        const uint32_t fc
        )
{
    return dispatch_reduce_dense_row_by_all_pivots_ff_32(dr, mat, bs, pc, pivs, dpivs, fc);
}

cf32_t *reduce_dense_row_by_dense_new_pivots_ff_32(
        int64_t *dr,
        len_t *pc,
        cf32_t * const * const pivs,
        const len_t ncr,
        const uint32_t fc
        )
{
    return dispatch_reduce_dense_row_by_dense_new_pivots_ff_32(dr, pc, pivs, ncr, fc);
}
