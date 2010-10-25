#!/usr/bin/env python

"""

jmyers oct 7 2010

Our mega-analysis script for the output of linkTracklets (check out
that filename!)

The idea is to make a single pass over each .byDiaIds file (tracks,
separated by newlines, each track as expressed as a set of Dia IDs,
separated by whitespace).  Since we tend to pay 'per-pass' in terms of
runtime, this will be a lot faster than running individual scripts to
do each query.

Also needed is a file which maps diaId to ssmId and obsHistId.  This
way we can judge whether a track was true, its corresponding object,
and pair track counts to their start (and possibly someday, end)
images.

So, here are the parameters:

diaDataDump tracksFile statsFile per-obsHistCountsFile foundObjectsFile

the diaDataDump is the file containing (whitespace-delimited fields,
newline-delimited lines):

diaId observingTime(MJD) objectID obsHistId(image where diaId was observed) 



tracksFile will be the tracks from linkTracklets.
-------------------------------------------------------

statsFile will get the following data:

- total tracks in the tracksFile
- true tracks in the tracksFile
- false tracks in the tracksFile
- number of unique findable objects in the tracksFile (objects with some true track)



per-obsHistCountsFile gets the following:
--------------------------------------------------------

for every obsHistId, the number of tracks STARTING IN that image, in the format:

obsHistId numTracks



foundObjectsFile gets the following:
--------------------------------------------------------
the name of every object found (associated with some true track), newline-delimited.

"""


import sys

FALSE_DIA_SSM_ID="-1" # the ssmId of a DiaSource which is attributable to non-asteroid sources


def readDias(diasDataFile):

    """ reads a dump of dias, which include diaId expMjd ssmId
    obsHistId for every diaSource.  Returns a dict mapping diaId to
    data."""
    idToDias = {}
    line = diasDataFile.readline()
    while line != "":
        [diaId, expMjd, ssmId, obsHistId] = line.split()
        diaId = int(diaId)
        expMjd = float(expMjd)
        obsHistId = int(obsHistId)
        
        idToDias[diaId] = diaSource(diaId=diaId, obsTime=expMjd, ssmId=ssmId, obsHistId=obsHistId)
        
        line = diasDataFile.readline()

    return idToDias


def lookUpDia(diasLookupDict, diaId):
    return diasLookupDict[diaId]



class diaSource:
    def __init__(self, diaId, obsTime, ssmId, obsHistId):
        self.diaId = diaId
        self.obsTime = obsTime
        self.ssmId = ssmId
        self.obsHistId = obsHistId

    def getDiaId(self):
        return self.diaId

    def getObsTime(self):
        return self.obsTime

    def getSsmId(self):
        return self.ssmId

    def getObsHistId(self):
        return self.obsHistId






def getLotsOfStatsFromTracksFile(diasLookupDict, tracksFile, trueTracksOutFile):

    """return number of true tracks, number of false tracks, a
    dictionary mapping obsHistId (image ID) to true/false track counts
    (as a two-part list), and the set of findable objects

    also, whenever a true track is found, it is written to trueTracksOutFile
    """

    trackLine = tracksFile.readline()

    nTrue = 0
    nFalse = 0
    obsHistCounts = {}
    foundObjects = set()
    
    while trackLine != "":
        diaIds = map(int, trackLine.split())
        dias = map(lambda x: lookUpDia(diasLookupDict, x), diaIds)

        ssmIds = set(map(lambda x: x.getSsmId(), dias))

        #figure out the first obs time and the obsHistId associated with it
        minObsTime = None
        firstObsHistId = None
        for dia in dias:
            obsTime = dia.getObsTime()
            if minObsTime == None:
                minObsTime = obsTime
                firstObsHistId = dia.getObsHistId()
            elif minObsTime > obsTime:
                minObsTime = obsTime
                firstObsHistId = dia.getObsHistId()

        if obsHistCounts.has_key(firstObsHistId):
            [trueAtImage, falseAtImage] = obsHistCounts[firstObsHistId]
        else:
            trueAtImage = 0
            falseAtImage = 0
            
        #figure out if this is a true track, act accordingly
        if (len(ssmIds) == 1) and (FALSE_DIA_SSM_ID not in ssmIds):
            nTrue += 1
            isTrueTrack = True
            objName = dias[0].ssmId
            foundObjects.add(objName)
            trueAtImage += 1
            trueTracksOutFile.write(trackLine.strip() + '\n')
        else:
            nFalse += 1
            falseAtImage += 1

        #update obsHistCounts
        obsHistCounts[firstObsHistId] = [trueAtImage, falseAtImage]
        
        trackLine = tracksFile.readline()

    return nTrue, nFalse, obsHistCounts, foundObjects




def writeStatsFile(nTrue, nFalse, foundObjects, statsOutFile):
    statsOutFile.write("!num_total_tracks num_true_tracks    num_false_tracks    num_found_objects\n")
    statsOutFile.write("%d %d %d %d\n" % (nTrue+nFalse, nTrue, nFalse, len(foundObjects)))


def writeObsHistFile(obsHistCounts, obsHistCountsOut):
    obsHistCountsOut.write("!obsHistId  nTracks_startinghere nTrueTracks_startinghere nFalseTracks_startinghere\n")
    for obsHistId in sorted(obsHistCounts.keys()):
        [nTrue, nFalse] =  obsHistCounts[obsHistId]
        obsHistCountsOut.write("%d %d %d %d\n" % (obsHistId, nTrue + nFalse, nTrue, nFalse))


def writeFoundObjectsFile(foundObjects, foundObjectsOut):
    for foundObject in foundObjects:
        foundObjectsOut.write("%s\n" % foundObject)
    


if __name__=="__main__":
    import time
    "Starting analysis at ", time.ctime()

    if len(sys.argv) != 7:
        print "USAGE: ", sys.argv[0], " diaDataDump tracksFile statsOutFile per-obsHistCountsOutFile foundObjectsOutFile trueTracks"
        sys.exit(1)
        
    [diaDataDump, tracks, statsOut, obsHistCountsOut, foundObjectsOut, trueTracksOut] = sys.argv[1:]

    print "Reading diaSource info from ", diaDataDump
    print "Reading tracks from ", tracks
    print "Printing basic track stats to ", statsOut
    print "Pringing per-image track stats to ", obsHistCountsOut
    print "Printing names of found objects to ", foundObjectsOut
    print "Printing true tracks to ", trueTracksOut

    diasDataFile = file(diaDataDump,'r')
    tracksFile = file(tracks,'r')
    statsOutFile = file(statsOut,'w')
    obsHistOutFile = file(obsHistCountsOut,'w')
    foundObjectsOutFile = file(foundObjectsOut,'w')
    trueTracksOutFile = file(trueTracksOut, 'w')

    print "Reading dump of all Dias at ", time.ctime()
    diasLookupDict = readDias(diasDataFile)
    print "Done. Starting analysis at ", time.ctime()
    t0 = time.time()

    nTrue, nFalse, obsHistCounts, foundObjects = getLotsOfStatsFromTracksFile(diasLookupDict, tracksFile, trueTracksOutFile)
    print "Done at ", time.ctime()
    dt = time.time() - t0
    print "Reading/analyzing ", nTrue + nFalse, " tracks took ", dt, " seconds."
    print "Writing output at ", time.ctime()
    writeStatsFile(nTrue, nFalse, foundObjects, statsOutFile)
    writeObsHistFile(obsHistCounts, obsHistOutFile)
    writeFoundObjectsFile(foundObjects, foundObjectsOutFile)

    statsOutFile.close()
    obsHistOutFile.close()
    foundObjectsOutFile.close()
    
    print "Analysis DONE and output written successfully at ", time.ctime()
