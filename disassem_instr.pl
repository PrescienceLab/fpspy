#!/usr/bin/perl -w

#
# Part of FPSpy
#
# Copyright (c) 2018 Peter Dinda - see LICENSE
#

$#ARGV==-1 or $#ARGV==0 or die "usage: decode_instr.pl hexbytes or < hexbytes\n";

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
    
    print S ".code64\n.text\n.global foo\nfoo:\n";
    for ($i=0;$i<length($hex);$i+=2) {
	print S "  .byte 0x", substr($hex,$i,2),"\n";
    }
    
    close(S);
    
    system("rm -f __test.o && gcc -c __test.S -o __test.o");
    
    open(D,"objdump -d __test.o |") or die "can't disassemble\n";
    
    while (my $l=<D>) {
	chomp($l);
	if ($l=~/^\s+0\:/) {
	    my @cols = split(/\t/,$l);
	    $cols[2]=~s/\#.*$//g;
	    my $outhex = $cols[1];
	    my $instr = $cols[2]; # always the case
	    # now consider next two lines, just in case this is a long instruction
	    # just to get the output hex right (up to 15 bytes, so 3 lines at 7 bytes each
	    for (my $i=0;$i<2;$i++) {
		if (defined($l=<D>)) {
		    chomp($l);
		    @cols = split(/\t/,$l);
		    if ($#cols==1) { # continuation
			$outhex.=$cols[1];
		    } else {
			last;
		    }
		}
	    }
	    system("rm -f __test.o __test.S");
	    close(D);
	    return ($outhex,$instr);
	}
    }
    close(D);
    return undef;
}



	


    
