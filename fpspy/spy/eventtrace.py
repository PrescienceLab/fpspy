import sys
import fileinput
import csv
if(len(sys.argv)!=2):
    sys.exit("Error - needs time interval in ms")
interval = int(sys.argv[1])
prevmillis = 0
events = 0
with open('trace.csv', 'w+') as csvfile:
    csvwriter = csv.writer(csvfile, delimiter=' ', quotechar='|', quoting=csv.QUOTE_MINIMAL)
    csvwriter.writerow(['Time,','Events'])
    for line in sys.stdin:
        results = line.split()
        millis = int(results[1])
        if(millis-prevmillis<interval):
            events = events +1
        else:
            csvwriter.writerow([prevmillis+",",events])
            prevmillis=millis
            events=0
    
