#!/usr/bin/perl -w

$#ARGV==0 or die "usage: parse_individual.pl file\n";

%decode = ( 1 => "FPE_INTDIV",
	    2 => "FPE_INTOVF",
	    3 => "FPE_FLTDIV",
	    4 => "FPE_FLTOVF",
	    5 => "FPE_FLTUND",
	    6 => "FPE_FLTRES",
	    7 => "FPE_FLTINV",
	    8 => "FPE_FLTSUB");

$file = shift;

open(RAW,'<:raw', $file) or die "Failed to open $file\n";


while (1) {
    $n = read(RAW,$rec, 24);
    last if ($n!=24);
    ($rip, $rsp, $code, $mxcsr) = unpack("QQLL",$rec);
    $dec = $decode{$code};
    if (!defined($dec)) { 
	$dec = "UNDEF"
    }
    print sprintf("%s\t%016x\t%016x\t%08x\t%08x\n",$dec, $rip,$rsp,$code,$mxcsr);
}
	
