#
# Part of FPSpy
#
# Copyright (c) 2019 Alex Bernat - see LICENSE
#


import os
import sys
import commands
import subprocess
sys.path.append(os.getcwd())
debug = 0
if(len(sys.argv)==1):
	print("Need input file")
else:
	infile = sys.argv[1]
	status, output = commands.getstatusoutput("./analyze_individual_auto.pl "+infile)
	if(debug ==1):
		print(output)
	instrhex = output.splitlines()
	if(debug==1):
		print(instrhex)
	instrcounts = []
	for i in instrhex:
		instrcounts.append(i.split())
	if(debug==1):
		print(instrcounts)

	instr = []
	for cmd in instrcounts:
		statusdis, outputinstr = commands.getstatusoutput("./disassem_instr.pl  "+cmd[1])	
		print(cmd[0]+"\t"+ outputinstr)






#remember to truncate(0) analyze.out
