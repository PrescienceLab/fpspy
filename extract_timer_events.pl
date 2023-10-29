#!/usr/bin/perl -w

#
# Part of FPSpy
#
# Copyright (c) 2018 Peter Dinda - see LICENSE
#


#
# parse debug output and extract timing
#

while (<STDIN>) {
    if (/reinitialized for (\d+) us state (\S+)/) {
	print $1, "\n";
    }
}

