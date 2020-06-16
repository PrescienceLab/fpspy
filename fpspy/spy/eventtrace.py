#
# Part of FPSpy
#
# Copyright (c) 2019 Alex Bernat - see LICENSE
#

import sys
import fileinput
import csv
if(len(sys.argv)!=2):
    sys.exit("Error - needs time interval in ms")
interval = int(sys.argv[1])
events = 0
first_line = sys.stdin.readline()
first_line=first_line.split()
initialmillis= int(first_line[0])
prevmillis = int(first_line[0])
with open('trace.csv', 'w+') as csvfile:
    csvwriter = csv.writer(csvfile, delimiter=' ', quotechar='|', quoting=csv.QUOTE_MINIMAL)
    csvwriter.writerow(['Time,','Events'])
    for line in sys.stdin:
        results = line.split()
        millis = int(results[0])
        
        events = events + 1

        if(millis-prevmillis<interval):
            events = events +1
        else:
            csvwriter.writerow([str(prevmillis)+",",str(events)])
            prevmillis=millis
            events=0
           
