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

#ifndef MSOLVE_DATA_H
#define MSOLVE_DATA_H

#define _GNU_SOURCE
#include "../neogb/data.h"
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <ctype.h>
#include "../neogb/libneogb.h"

#define MODP(a,b)                               \
	(a) % (b)
typedef len_t nelts_t;
typedef int32_t nvars_t;

typedef struct{
  int32_t nvars;
  int32_t ngens;
  int32_t field_char;
  /* counts change of variable orders:
   * x1 <-> xn
   * x1 <-> xn-1
   * ...
   * for situation when staircase is not generic enough */
  int32_t change_var_order;
  /* base coefficient for linear form
   * sum(i^k*x[k]) k = 1, ..., nvars
   * It is zero if no linear form is active, otherwise != zero. */
  int32_t linear_form_base_coef;
  /* set to 1 if a linear form is chosen randomly */
  int32_t rand_linear;
  int32_t *random_linear_form;
  char **vnames;
  int32_t *lens;
  int32_t *exps;
  int32_t *cfs;
  mpz_t **mpz_cfs;
} data_gens_ff_t;

typedef struct{
  int32_t length;
  int32_t alloc;
  mpz_t *coeffs;
} mpz_upoly_struct;

typedef mpz_upoly_struct mpz_upoly_t[1];

typedef struct{
  long nvars;
  long nsols;
  long dquot;
  int dim;
  mpz_upoly_t elim;
  mpz_upoly_t denom;
  mpz_upoly_t *coords;
  mpz_t *cfs;
} mpz_param_struct;

typedef mpz_param_struct mpz_param_t[1];

typedef struct{
  mpz_t val_up;
  mpz_t val_do;
  long k_up;
  long k_do;
  unsigned int isexact;
} coord_struct;
typedef coord_struct coord_t[1];

typedef struct{
  long nvars;
  coord_t *coords;
} real_point_struct;
typedef real_point_struct real_point_t[1];

typedef struct{
  char *in_file;
  char *out_file;
} files_gb;
#endif