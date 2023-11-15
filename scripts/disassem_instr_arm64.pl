#!/usr/bin/perl -w

#
# Part of FPSpy
#
# Copyright (c) 2023 Peter Dinda - see LICENSE
#

$#ARGV==-1 or $#ARGV==0 or die "usage: decode_instr_arm64.pl hexbytes or < hexbytes\n";

if ($#ARGV==0) {
    $hex = shift; chomp($hex);
    ($dechex,$instr) = doit($hex);
    print $dechex,"\t",$instr,"\n";
} else {
    while ($hex=<STDIN>) {
	chomp($hex);
	($dechex,$instr) = doit($hex);
	print $dechex,"\t",$instr,"\n";
    }
}
	

sub doit {
    my $hex = shift;
    my $i;

    open(S,">__test.S") or die "Can't open intermediate file\n";

    
    print S ".text\n.global foo\nfoo:\n";
    # we care only about the first 4 bytes in all cases
    # since instructions are always the same length
    for ($i=0;$i<8;$i+=2) {
	print S "  .byte 0x", substr($hex,$i,2),"\n";
    }
    
    close(S);
    
    system("rm -f __test.o && gcc -c __test.S -o __test.o");

    # this only works on a "-D" on ARM for some reason I cannot
    # fathom... 
    open(D,"objdump -D __test.o |") or die "can't disassemble\n";
    
    while (my $l=<D>) {
	chomp($l);
	# on ARM, the instruction will aways be on one line...
	if ($l =~ /^\s+0\:\s+(\S+)\s+(\S.*)$/) {
	    my $outhex = $1;
	    my $instr = $2;

	    $instr=~s/\t/ /g;
#	    print "outhex=$outhex, instr=$instr\n";
	    
	    return ($outhex,$instr);
	}
    }
    close(D);
    return undef;
}



	


    
