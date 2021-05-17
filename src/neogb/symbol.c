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

static void select_spairs_by_minimal_degree(
        mat_t *mat,
        const bs_t * const bs,
        ps_t *psl,
        stat_t *st,
        ht_t *sht,
        ht_t *bht,
        ht_t *tht
        )
{
    len_t i, j, k, l, md, nps, npd, nrr = 0, ntr = 0;
    hm_t *b;
    deg_t d = 0;
    len_t load = 0;
    hi_t lcm;
    len_t *gens;
    exp_t *elcm, *eb;
    exp_t *etmp = bht->ev[0];

    /* timings */
    double ct0, ct1, rt0, rt1;
    ct0 = cputime();
    rt0 = realtime();

    spair_t *ps     = psl->p;
    const len_t nv  = bht->nv;

    /* sort pair set */
    sort_r(ps, (unsigned long)psl->ld, sizeof(spair_t), spair_degree_cmp, bht);
    /* get minimal degree */
    md  = bht->hd[ps[0].lcm].deg;

    /* select pairs of this degree respecting maximal selection size mnsel */
    for (i = 0; i < psl->ld; ++i) {
        if (bht->hd[ps[i].lcm].deg > md) {
            break;
        }
    }
    npd  = i;
    sort_r(ps, (unsigned long)npd, sizeof(spair_t), spair_cmp, bht);
    /* now do maximal selection if it applies */
    
    /* if we stopped due to maximal selection size we still get the following
     * pairs of the same lcm in this matrix */
    if (npd > st->mnsel) {
        nps = st->mnsel;
        lcm = ps[nps].lcm;
        while (nps < npd && ps[nps+1].lcm == lcm) {
            nps++;
        }
    } else {
        nps = npd;
    }
    if (st->info_level > 1) {
        printf("%3d  %6d %7d", md, nps, psl->ld);
        fflush(stdout);
    }
    /* statistics */
    st->num_pairsred  +=  nps;
    /* list for generators */
    gens  = (len_t *)malloc(2 * (unsigned long)nps * sizeof(len_t));
    /* preset matrix meta data */
    mat->rr       = (hm_t **)malloc(2 * (unsigned long)nps * sizeof(hm_t *));
    hm_t **rrows  = mat->rr;
    mat->tr       = (hm_t **)malloc(2 * (unsigned long)nps * sizeof(hm_t *));
    hm_t **trows  = mat->tr;
    mat->sz = 2 * nps;
    mat->nc = mat->ncl = mat->ncr = 0;
    mat->nr = 0;

    i = 0;

    while (i < nps) {
        /* ncols initially counts number of different lcms */
        mat->nc++;
        load  = 0;
        lcm   = ps[i].lcm;
        j = i;

        while (j < nps && ps[j].lcm == lcm) {
            gens[load++] = ps[j].gen1;
            gens[load++] = ps[j].gen2;
            ++j;
        }
        /* sort gens set */
        qsort(gens, (unsigned long)load, sizeof(len_t), gens_cmp);

        len_t prev  = -1;

        /* first element with given lcm goes into reducer part of matrix,
         * all remaining ones go to to be reduced part */
        prev  = gens[0];
        /* ev might change when enlarging the hash table during insertion of a new
            * row in the matrix, thus we have to reset elcm inside the for loop */
        elcm  = bht->ev[lcm];
        d     = 0;
        b     = bs->hm[prev];
        eb    = bht->ev[b[OFFSET]];
        for (l = 0; l < nv; ++l) {
            etmp[l] = (exp_t)(elcm[l] - eb[l]);
            d     +=  etmp[l];
        }
        const hi_t h    = bht->hd[lcm].val - bht->hd[b[OFFSET]].val;
        /* note that we use index mat->nc and not mat->nr since for each new
         * lcm we add exactly one row to mat->rr */
        rrows[nrr]  = multiplied_poly_to_matrix_row(sht, bht, h, d, etmp, b);
        /* track trace information ? */
        if (tht != NULL) { 
           rrows[nrr][BINDEX]  = prev;
            if (tht->eld == tht->esz-1) {
                enlarge_hash_table(tht);
            }
            rrows[nrr][MULT]    = insert_in_hash_table(etmp, tht);
        }

        /* mark lcm column as lead term column */
        sht->hd[rrows[nrr++][OFFSET]].idx = 2; 
        /* still we have to increase the number of rows */
        mat->nr++;
        for (k = 1; k < load; ++k) {
            /* check sorted list for doubles */
            if (gens[k] ==  prev) {
                continue;
            }
            prev  = gens[k];
            /* ev might change when enlarging the hash table during insertion of a new
             * row in the matrix, thus we have to reset elcm inside the for loop */
            elcm  = bht->ev[lcm];
            d     = 0;
            b     = bs->hm[prev];
            eb    = bht->ev[b[OFFSET]];
            for (l = 0; l < nv; ++l) {
                etmp[l] = (exp_t)(elcm[l] - eb[l]);
                d     +=  etmp[l];
            }
            const hi_t h  = bht->hd[lcm].val - bht->hd[b[OFFSET]].val;
            trows[ntr] = multiplied_poly_to_matrix_row(sht, bht, h, d, etmp, b);
            /* track trace information ? */
            if (tht != NULL) {
                trows[ntr][BINDEX]  = prev;
                if (tht->eld == tht->esz-1) {
                    enlarge_hash_table(tht);
                }
                trows[ntr][MULT]    = insert_in_hash_table(etmp, tht);
            }
            /* mark lcm column as lead term column */
            sht->hd[trows[ntr++][OFFSET]].idx = 2;
            mat->nr++;
        }

        i = j;
    }
    /* fix rows to be reduced */
    mat->tr = realloc(mat->tr, (unsigned long)(mat->nr - mat->nc) * sizeof(hm_t *));

    st->num_rowsred +=  mat->nr - mat->nc;
    st->current_deg =   md;

    free(gens);

    /* remove selected spairs from pairset */
    memmove(ps, ps+nps, (unsigned long)(psl->ld-nps) * sizeof(spair_t));
    psl->ld -=  nps;

    /* timings */
    ct1 = cputime();
    rt1 = realtime();
    st->select_ctime  +=  ct1 - ct0;
    st->select_rtime  +=  rt1 - rt0;
}

static void select_tbr(
        const bs_t * const tbr,
        const exp_t * const mul,
        const len_t start,
        mat_t *mat,
        stat_t *st,
        ht_t *sht,
        ht_t *bht,
        ht_t *tht
        )
{
    len_t i;

    len_t ntr = 0;

    /* preset matrix meta data */
    mat->rr       = (hm_t **)malloc(100 * sizeof(hm_t *));
    mat->tr       = (hm_t **)malloc((unsigned long)tbr->ld * sizeof(hm_t *));
    hm_t **trows  = mat->tr;

    mat->sz = 100;
    mat->nc = mat->ncl = mat->ncr = 0;
    mat->nr = 0;

    /* always take all elements in tbr and
     * multiply them by the given multiple */
    for (i = start; i < tbr->ld; ++i) {
        const hm_t *b   = tbr->hm[i];
        /* remove the multiplier business for the moment, no need
         * and it corrupts a bit the sht size for efficient matrix
         * generation */
        /* const hi_t mulh = insert_in_hash_table(mul, sht);
         * const hi_t h    = sht->hd[mulh].val;
         * const deg_t d   = sht->hd[mulh].deg; */
        const hi_t h    = 0;
        const deg_t d   = 0;
        trows[ntr++]    = multiplied_poly_to_matrix_row(
                sht, bht, h, d, mul, b);
        mat->nr++;
    }
}


static inline void find_multiplied_reducer(
        const bs_t * const bs,
        const hm_t m,
        const ht_t * const bht,
        len_t *nr,
        hm_t **rows,
        ht_t *sht,
        ht_t *tht
        )
{
    len_t i, k;

    const len_t rr  = *nr;

    const len_t nv  = bht->nv;

    const exp_t * const e  = sht->ev[m];

    const hd_t hdm    = sht->hd[m];
    const len_t lml   = bs->lml;
    const sdm_t ns    = ~hdm.sdm;
    const deg_t hdd   = hdm.deg;

    const sdm_t * const lms = bs->lm;
    const bl_t * const lmps = bs->lmps;

    exp_t *etmp = bht->ev[0];
    const hd_t * const hdb  = bht->hd;
    exp_t * const * const evb = bht->ev;

    i = 0;
start:
    while (i < lml && lms[i] & ns) {
        i++;
    }
    if (i < lml) {
        const hm_t *b = bs->hm[lmps[i]];
        const deg_t d = hdd - hdb[b[OFFSET]].deg;
        if (d < 0) {
            i++;
            goto start;
        }
        const exp_t * const f = evb[b[OFFSET]];
        for (k=0; k < nv; ++k) {
            etmp[k] = (exp_t)(e[k]-f[k]);
            if (etmp[k] < 0) {
                i++;
                goto start;
            }
        }
        const hi_t h  = hdm.val - hdb[b[OFFSET]].val;
        rows[rr]  = multiplied_poly_to_matrix_row(sht, bht, h, d, etmp, b);
        /* track trace information ? */
        if (tht != NULL) {
            rows[rr][BINDEX]  = lmps[i];
            if (tht->eld == tht->esz-1) {
                enlarge_hash_table(tht);
            }
            rows[rr][MULT]    = insert_in_hash_table(etmp, tht);
        }
        sht->hd[m].idx  = 2;
        *nr             = rr + 1;
    }
}

static void symbolic_preprocessing(
        mat_t *mat,
        const bs_t * const bs,
        stat_t *st,
        ht_t *sht,
        ht_t *tht,
        const ht_t * const bht
        )
{
    hl_t i;

    /* timings */
    double ct0, ct1, rt0, rt1;
    ct0 = cputime();
    rt0 = realtime();

    /* at the moment we have as many reducers as we have different lcms */
    len_t nrr = mat->nc; 

    /* note that we have already counted the different lcms, i.e.
     * ncols until this step. moreover, we have also already marked
     * the corresponding hash indices to represent lead terms. so
     * we only have to do the bookkeeping for newly added reducers
     * in the following. */

    const hl_t oesld = sht->eld;
    const len_t onrr  = mat->nc;
    i = 1;
    /* we only have to check if idx is set for the elements already set
     * when selecting spairs, afterwards (second for loop) we do not
     * have to do this check */
    while (mat->sz <= nrr + oesld) {
        mat->sz *=  2;
        mat->rr =   realloc(mat->rr, (unsigned long)mat->sz * sizeof(hm_t *));
    }
    for (; i < oesld; ++i) {
        if (!sht->hd[i].idx) {
            sht->hd[i].idx = 1;
            mat->nc++;
            find_multiplied_reducer(bs, i, bht, &nrr, mat->rr, sht, tht);
        }
    }
    for (; i < sht->eld; ++i) {
        if (mat->sz == nrr) {
            mat->sz *=  2;
            mat->rr  =  realloc(mat->rr, (unsigned long)mat->sz * sizeof(hm_t *));
        }
        sht->hd[i].idx = 1;
        mat->nc++;
        find_multiplied_reducer(bs, i, bht, &nrr, mat->rr, sht, tht);
    }
    /* realloc to real size */
    mat->rr   =   realloc(mat->rr, (unsigned long)nrr * sizeof(hm_t *));
    mat->nr   +=  nrr - onrr;
    mat->nrl  =   mat->nr - nrr;
    mat->nru  =   nrr;
    mat->sz   =   mat->nr;
    mat->rbal =   mat->nrl;

    /* initialize memory for reducer bit arrays for tracing information */
    mat->rba  = (rba_t **)malloc((unsigned long)mat->rbal * sizeof(rba_t *));
    const unsigned long len = nrr / 32 + ((nrr % 32) != 0);
    for (i = 0; i < mat->nrl; ++i) {
        mat->rba[i] = (rba_t *)calloc(len, sizeof(rba_t));
    }

    /* statistics */
    st->max_sht_size  = st->max_sht_size > sht->esz ?
        st->max_sht_size : sht->esz;

    /* timings */
    ct1 = cputime();
    rt1 = realtime();
    st->symbol_ctime  +=  ct1 - ct0;
    st->symbol_rtime  +=  rt1 - rt0;
}

static void generate_matrix_from_trace(
        mat_t *mat,
        const trace_t * const trace,
        const len_t idx,
        const bs_t * const bs,
        stat_t *st,
        ht_t *sht,
        const ht_t * const bht,
        const ht_t * const tht
        )
{
    /* timings */
    double ct0, ct1, rt0, rt1;
    ct0 = cputime();
    rt0 = realtime();

    len_t i, nr;
    hm_t *b;
    deg_t d;
    exp_t *emul;
    hi_t h;

    td_t td       = trace->td[idx];
    mat->rr       = (hm_t **)malloc((unsigned long)td.rld * sizeof(hm_t *));
    hm_t **rrows  = mat->rr;
    mat->tr       = (hm_t **)malloc((unsigned long)td.tld * sizeof(hm_t *));
    hm_t **trows  = mat->tr;
    mat->rba      = (rba_t **)malloc((unsigned long)td.tld * sizeof(rba_t *));
    rba_t **rba   = mat->rba;

    /* reducer rows, i.e. AB part */
    i   = 0;
    nr  = 0;
    while (i < td.rld) {
        b     = bs->hm[td.rri[i++]];
        emul  = tht->ev[td.rri[i]];
        h     = tht->hd[td.rri[i]].val;
        d     = tht->hd[td.rri[i++]].deg;

        rrows[nr] = multiplied_poly_to_matrix_row(sht, bht, h, d, emul, b);
        sht->hd[rrows[nr][OFFSET]].idx = 2;
        ++nr;

    }
    /* to be reduced rows, i.e. CD part */
    i   = 0;
    nr  = 0;
    while (i < td.tld) {
        b     = bs->hm[td.tri[i++]];
        emul  = tht->ev[td.tri[i]];
        h     = tht->hd[td.tri[i]].val;
        d     = tht->hd[td.tri[i]].deg;

        trows[nr] = multiplied_poly_to_matrix_row(sht, bht, h, d, emul, b);
        /* At the moment rba is unused */
        rba[nr]   = td.rba[i/2];
        i++;
        nr++;
    }
    /* meta data for matrix */
    mat->nru  = td.rld/2;
    mat->nrl  = td.tld/2;
    mat->nr   = mat->sz = mat->nru + mat->nrl;
    mat->nc   = sht->eld-1;

    /* statistics */
    st->max_sht_size  = st->max_sht_size > sht->esz ?
        st->max_sht_size : sht->esz;

    /* timings */
    ct1 = cputime();
    rt1 = realtime();
    st->symbol_ctime  +=  ct1 - ct0;
    st->symbol_rtime  +=  rt1 - rt0;
}