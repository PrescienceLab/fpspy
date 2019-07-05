#!/usr/bin/perl -w

#
# parse debug output and extract timing
#

while (<STDIN>) {
    if (/reinitialized for (\d+) us state (\S+)/) {
	print $1, "\n";
    }
}

