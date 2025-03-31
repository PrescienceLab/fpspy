#!/usr/bin/perl -w

#
# Part of FPSpy
#
# Copyright (c) 2023 Peter Dinda - see LICENSE
#

$#ARGV==-1 or $#ARGV==0 or die "usage: decode_instr.pl hexbytes or < hexbytes\n";

if ($ENV{FPSPY_ARCH}) {
    $myarch=$ENV{FPSPY_ARCH};
} else {
    $myarch=`uname -m`;  chomp($myarch);
    
    if ($myarch eq "x86_64") { $myarch = "x64";}
    if ($myarch eq "aarch64") { $myarch = "arm64";}
    if ($myarch eq "riscv64") { $myarch = "riscv64";}
}
    

$cmd = "disassem_instr_$myarch.pl";
if ($#ARGV==0) {
    $cmd .= " ".$ARGV[0];
} 

system $cmd;

