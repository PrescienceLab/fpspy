#!/usr/bin/perl -w

#
# Part of FPSpy
#
# Copyright (c) 2018 Peter Dinda - see LICENSE
#


$#ARGV==0 or $#ARGV==1 or die "analyze_individual.pl file [disassem]\n";

$file = shift;

$dis = $#ARGV==0;

open(S,"parse_individual.pl $file |") or die "cannot open\n";

$n=0;

while (<S>) {
    chomp;
    @cols = split(/\s+/);
    #    $ts=$cols[0];
    #    $type=$cols[1];
    $rip=$cols[2];
    #    $sp=$cols[3]
    $inst=$cols[6];
    $rips{$rip}++;
    $insts{$inst}++;
    $n++;
}

close(S);

print "Saw $n events:\n";

print "\nBy RIP\n";
map { print  "\t", $rips{$_}, "\t", $_, "\n";  }    sort { $rips{$b} <=> $rips{$a} }   keys %rips;

print "\nBy Instruction\n";
map {
    if ($dis) {
	$dis_instr = `disassem_instr.pl $_`;
	$dis_instr =~ s/.*\t(.*)/$1/g;
	chomp($dis_instr);
	print  "\t", $insts{$_}, "\t", $_, "\t", $dis_instr, "\n";
    } else {
	print  "\t", $insts{$_}, "\t", $_, "\n";
    }
} sort { $insts{$b} <=> $insts{$a} }  keys %insts;
    

