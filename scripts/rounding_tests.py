#
# Part of FPSpy
#
# Copyright (c) 2020 Alex Bernat - see LICENSE
#

#! /usr/bin/python python
import sys
import os

program_command = "YOUR_COMMAND_HERE" #whatever you type at the command line to start the program (without the mpirun if the program uses that)
output_file_match = "YOUR_OUTFILE_HERE" #some expression that will match the program's outfile name to move it (eg. *miniaero*.yaml) 

if(len(sys.argv) != 3):
	sys.exit("needs mpi or nompi and num threads (1 if nompi)")
threads = sys.argv[2]
if(sys.argv[1]=="mpi"):
	print("running in MPI mode")
	print("running baseline test")
	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
	os.system("mkdir rounding_baseline")
	os.system("mv " +output_file_match+" rounding_baseline")
	os.system("mv *fpemon rounding_baseline")
	
	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=nearest -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
	os.system("mkdir nearest")
        os.system("mv " +output_file_match+" nearest")
	os.system("mv *fpemon nearest")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=nearest,ftz -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir nearest_ftz")
        os.system("mv " +output_file_match+" nearest_ftz")
        os.system("mv *fpemon nearest_ftz")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=nearest,daz -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir nearest_daz")
        os.system("mv " +output_file_match+" nearest_daz")
        os.system("mv *fpemon nearest_daz")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=nearest,daz,ftz -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir nearest_daz_ftz")
        os.system("mv " +output_file_match+" nearest_daz_ftz")
        os.system("mv *fpemon nearest_daz_ftz")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=zero -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir zero")
        os.system("mv " +output_file_match+" zero")
        os.system("mv *fpemon zero")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=zero,ftz -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir zero_ftz")
        os.system("mv " +output_file_match+" zero_ftz")
        os.system("mv *fpemon zero_ftz")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=zero,daz -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir zero_daz")
        os.system("mv " +output_file_match+" zero_daz")
        os.system("mv *fpemon zero_daz")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=zero,daz,ftz -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir zero_daz_ftz")
        os.system("mv " +output_file_match+" zero_daz_ftz")
        os.system("mv *fpemon zero_daz_ftz")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=positive -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir positive")
        os.system("mv " +output_file_match+" positive")
        os.system("mv *fpemon positive")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=positive,ftz -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir positive_ftz")
        os.system("mv " +output_file_match+" positive_ftz")
        os.system("mv *fpemon positive_ftz")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=positive,daz -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir positive_daz")
        os.system("mv " +output_file_match+" positive_daz")
        os.system("mv *fpemon positive_daz")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=positive,daz,ftz -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir positive_daz_ftz")
        os.system("mv " +output_file_match+" positive_daz_ftz")
        os.system("mv *fpemon positive_daz_ftz")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=negative -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir negative")
        os.system("mv " +output_file_match+" negative")
        os.system("mv *fpemon negative")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=negative,ftz -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir negative_ftz")
        os.system("mv " +output_file_match+" negative_ftz")
        os.system("mv *fpemon negative_ftz")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=negative,daz -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir negative_daz")
        os.system("mv " +output_file_match+" negative_daz")
        os.system("mv *fpemon negative_daz")

	os.system("mpirun -x FPSPY_MODE=individual -x FPSPY_AGGRESSIVE=yes -x FPSPY_EXCEPT_LIST=invalid -x FPSPY_FORCE_ROUNDING=negative,daz,ftz -x LD_PRELOAD=./fpspy.so -np  "+threads+" " +program_command)
        os.system("mkdir negative_daz_ftz")
        os.system("mv " +output_file_match+" negative_daz_ftz")
        os.system("mv *fpemon negative_daz_ftz")

	print("rounding tests concluded.")

elif(sys.argv[1]=="nompi"):
	print("running in non-mpi mode")
    	print("running baseline test")
        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir rounding_baseline")
        os.system("mv " +output_file_match+" rounding_baseline")
        os.system("mv *fpemon rounding_baseline")

    	os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=nearest LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir nearest")
        os.system("mv " +output_file_match+" nearest")
        os.system("mv *fpemon nearest")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=nearest,ftz LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir nearest_ftz")
        os.system("mv " +output_file_match+" nearest_ftz")
        os.system("mv *fpemon nearest_ftz")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=nearest,daz LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir nearest_daz")
        os.system("mv " +output_file_match+" nearest_daz")
        os.system("mv *fpemon nearest_daz")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=nearest,daz,ftz LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir nearest_daz_ftz")
        os.system("mv " +output_file_match+" nearest_daz_ftz")
        os.system("mv *fpemon nearest_daz_ftz")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=zero LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir zero")
        os.system("mv " +output_file_match+" zero")
        os.system("mv *fpemon zero")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=zero,ftz LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir zero_ftz")
        os.system("mv " +output_file_match+" zero_ftz")
        os.system("mv *fpemon zero_ftz")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=zero,daz LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir zero_daz")
        os.system("mv " +output_file_match+" zero_daz")
        os.system("mv *fpemon zero_daz")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=zero,daz,ftz LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir zero_daz_ftz")
        os.system("mv " +output_file_match+" zero_daz_ftz")
        os.system("mv *fpemon zero_daz_ftz")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=positive LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir positive")
        os.system("mv " +output_file_match+" positive")
        os.system("mv *fpemon positive")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=positive,ftz LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir positive_ftz")
        os.system("mv " +output_file_match+" positive_ftz")
        os.system("mv *fpemon positive_ftz")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=positive,daz LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir positive_daz")
        os.system("mv " +output_file_match+" positive_daz")
        os.system("mv *fpemon positive_daz")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=positive,daz,ftz LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir positive_daz_ftz")
        os.system("mv " +output_file_match+" positive_daz_ftz")
        os.system("mv *fpemon positive_daz_ftz")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=negative LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir negative")
        os.system("mv " +output_file_match+" negative")
        os.system("mv *fpemon negative")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=negative,ftz LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir negative_ftz")
        os.system("mv " +output_file_match+" negative_ftz")
        os.system("mv *fpemon negative_ftz")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=negative,daz LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir negative_daz")
        os.system("mv " +output_file_match+" negative_daz")
        os.system("mv *fpemon negative_daz")

        os.system("FPSPY_MODE=individual FPSPY_AGGRESSIVE=yes FPSPY_EXCEPT_LIST=invalid FPSPY_FORCE_ROUNDING=negative,daz,ftz LD_PRELOAD=./fpspy.so "+program_command)
        os.system("mkdir negative_daz_ftz")
        os.system("mv " +output_file_match+" negative_daz_ftz")
        os.system("mv *fpemon negative_daz_ftz")

        print("rounding tests concluded.")
else:
	sys.exit("We shouldn't be here! Check your command line arguments...")

