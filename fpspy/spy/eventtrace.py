import sys
import fileinput
import csv
if(len(sys.argv)!=2):
    sys.exit("Error - needs time interval in ms")
interval = int(sys.argv[1])
events = 0
with open('trace.csv', 'w+') as csvfile:
    csvwriter = csv.writer(csvfile, delimiter=' ', quotechar='|', quoting=csv.QUOTE_MINIMAL)
    csvwriter.writerow(['Time,','Events'])
    for line in sys.stdin:
        results = line.split()
        millis = int(results[0])
        initialmillis = millis
        prevmillis = millis
        events = events +1
        csvwriter.writerow([str(prevmillis)+",",str(events)])
        '''
        if(millis-prevmillis>=interval):
            csvwriter.writerow([str(prevmillis)+",",str(events)])
            prevmillis=millis
            events=0
            '''