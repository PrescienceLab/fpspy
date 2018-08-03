#!/usr/bin/perl -w

$#ARGV==0 or die "usage: parse_individual.pl file\n";

%decode = ( 1 => "FPE_INTDIV",
	    2 => "FPE_INTOVF",
	    3 => "FPE_FLTDIV",
	    4 => "FPE_FLTOVF",
	    5 => "FPE_FLTUND",
	    6 => "FPE_FLTRES",
	    7 => "FPE_FLTINV",
	    8 => "FPE_FLTSUB",
            0xffffffff => "***ABORT!!");

$file = shift;

open(RAW,'<:raw', $file) or die "Failed to open $file\n";


while (1) {
    $n = read(RAW,$rec, 32);  
    last if ($n!=32);
    ($time, $rip, $rsp, $code, $mxcsr) = unpack("QQQLL",$rec);
    $n = read(RAW,$instr, 15);
    last if ($n!=15);
    $n = read(RAW,$junk, 1); undef($junk);
    last if ($n!=1);
    $dec = $decode{$code};
    if (!defined($dec)) { 
	$dec = "UNDEF"
    }

    print sprintf("%-16ld\t%s\t%016x\t%016x\t%08x\t%08x\t",$time, $dec, $rip,$rsp,$code,$mxcsr);
    print unpack("H*",$instr), "\n";
}
	
