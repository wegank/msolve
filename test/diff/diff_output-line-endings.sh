#!/usr/bin/env bash

source test/diff/diff_source.sh

dos_output=test/diff/line_endings_dos.out
unix_output=test/diff/line_endings_unix.out

$(pwd)/msolve -f input_files/line_endings/in1_dos.ms -o "$dos_output" \
      --random-seed $seed \
      -l 2 -t 1
if [ $? -gt 0 ]; then
    print_exit 1
fi

perl -e 'local $/; open my $f, "<:raw", shift or exit 1; my $s = <$f>; exit(($s =~ /\r\n/ && $s !~ /\r(?!\n)/ && $s !~ /(^|[^\r])\n/) ? 0 : 1);' "$dos_output"
if [ $? -gt 0 ]; then
    print_exit 2
fi

$(pwd)/msolve -f input_files/line_endings/in1_unix.ms -o "$unix_output" \
      --random-seed $seed \
      -l 2 -t 1
if [ $? -gt 0 ]; then
    print_exit 3
fi

perl -e 'local $/; open my $f, "<:raw", shift or exit 1; my $s = <$f>; exit(($s =~ /\n/ && $s !~ /\r/) ? 0 : 1);' "$unix_output"
if [ $? -gt 0 ]; then
    print_exit 4
fi

rm "$dos_output" "$unix_output"

normal_exit
