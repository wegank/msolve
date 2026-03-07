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

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define THREAD_LOCAL _Thread_local
#else
#define THREAD_LOCAL __thread
#endif

static function_pointers_t function_pointers_template;
static uint64_t function_pointers_template_version = 1;

static THREAD_LOCAL function_pointers_t function_pointers_local;
static THREAD_LOCAL uint64_t function_pointers_local_version = 0;

function_pointers_t *get_function_pointers_table(void)
{
    if (function_pointers_local_version != function_pointers_template_version) {
        function_pointers_local = function_pointers_template;
        function_pointers_local_version = function_pointers_template_version;
    }
    return &function_pointers_local;
}

void publish_function_pointers_table(void)
{
    if (omp_in_parallel()) {
        return;
    }
    function_pointers_template = function_pointers_local;
    function_pointers_template_version++;
    if (function_pointers_template_version == 0) {
        function_pointers_template_version = 1;
    }
    function_pointers_local_version = function_pointers_template_version;
}
