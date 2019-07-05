#!/usr/bin/perl -w

$#ARGV==0 or die "usage: timestamps.pl fpemonfile\n";

$f = shift;

open(DATA,"./trace_print $f |") or die "Can't open $f\n";

while ($l=<DATA>) {
    @c = split(/\s+/,$l);
    print $c[0],"\n";
}

