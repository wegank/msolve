#!/usr/bin/env bash

#test file for real root extraction on a genuinely univariate system
#(a single variable in the input). The cubic has one exact rational
#root and two irrational roots extremely close together, which used
#to crash the real root extraction code (Issue #358).

file=univariate-cubic-close-roots

source test/diff/diff_source.sh

source test/diff/diff_source-real.sh

normal_exit
