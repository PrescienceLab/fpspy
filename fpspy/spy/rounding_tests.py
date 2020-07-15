#
# Part of FPSpy
#
# Copyright (c) 2020 Alex Bernat - see LICENSE
#

import sys
import os

program_command = "YOUR_COMMAND_HERE" #whatever you type at the commandline to start the program (without the mpirun if the program uses that)
output_file_match = "YOUR_OUTFILE_HERE" #some expression that will match the program's outfile name to move it (eg. *miniaero*.yaml) 

if(len(sys.argv != 3 ):
	sys.exit("needs mpi or nompi and num threads (1 if nompi)")
threads = sys.argv[2]
if(sys.argv[1]=="mpi"):
	print("running in MPI mode")
	print("running baseline test")
	os.system("mpirun -x FPE_MODE=aggregate -x FPE_AGGRESSIVE=yes -x LD_PRELOAD=./fpe_preload.so -np  "+threads+" " +program_command)
	os.system("mkdir rounding_baseline")
	os.system("mv " +output_file_match+" rounding_baseline")
	os.system("mv *.fpmon rounding_baseline")
	
	os.system("mpirun -x FPE_MODE=aggregate -x FPE_AGGRESSIVE=yes -x FPE_FORCE_ROUNDING=nearest -x LD_PRELOAD=./fpe_preload.so -np  "+threads+" " +program_command)        
	os.system("mkdir nearest")
        os.system("mv " +output_file_match+" nearest")
	os.system("mv *.fpmon nearest")

	os.system("mpirun -x FPE_MODE=aggregate -x FPE_AGGRESSIVE=yes -x FPE_FORCE_ROUNDING=nearest,ftz -x LD_PRELOAD=./fpe_preload.so -np  "+threads+" " +program_command)         
        os.system("mkdir nearest_ftz")
        os.system("mv " +output_file_match+" nearest_ftz")
        os.system("mv *.fpmon nearest_ftz")

	os.system("mpirun -x FPE_MODE=aggregate -x FPE_AGGRESSIVE=yes -x FPE_FORCE_ROUNDING=nearest,daz -x LD_PRELOAD=./fpe_preload.so -np  "+threads+" " +program_command)         
        os.system("mkdir nearest_daz")
        os.system("mv " +output_file_match+" nearest_daz")
        os.system("mv *.fpmon nearest_daz")

	os.system("mpirun -x FPE_MODE=aggregate -x FPE_AGGRESSIVE=yes -x FPE_FORCE_ROUNDING=nearest,daz,ftz -x LD_PRELOAD=./fpe_preload.so -np  "+threads+" " +program_command)         
        os.system("mkdir nearest_daz_ftz")
        os.system("mv " +output_file_match+" nearest_daz_ftz")
        os.system("mv *.fpmon nearest_daz_ftz")
