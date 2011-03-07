// -*- LSST-C++ -*-
/* jonathan myers */
#define BOOST_TEST_MODULE linkTracklets

#include "mpi.h"
#include <boost/test/included/unit_test.hpp>
#include <boost/current_function.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <cmath>

// for rand()
#include <cstdlib> 
// for printing timing info
#include <time.h>


#include "TrackSet.h"
#include "../Detection.h"
#include "../Tracklet.h"
#include "linkTracklets.h"
#include "../Exceptions.h"


bool Eq(double a, double b) 
{
    double epsilon = 1e-10;
    return (fabs(a - b) < epsilon);
}


void debugPrintTrackletsAndDets(std::vector<Detection> allDets, std::vector<Tracklet> allTracklets) 
{

    for (unsigned int i = 0; i < allDets.size(); i++) {
        Detection* curDet = &allDets.at(i);
        std::cout << curDet->getID() << "\t" << curDet->getRA() << "\t" << curDet->getDec() << '\n';
    }
    std::cout << "all tracklets:\n";

    for (unsigned int i = 0; i < allTracklets.size(); i++) {
        Tracklet* curTracklet = &allTracklets.at(i);
        std::set<unsigned int>::const_iterator dIter;
        for (dIter = curTracklet->indices.begin(); dIter != curTracklet->indices.end(); dIter++) {
            std::cout << *dIter << " ";
        }
        std::cout << '\n';
    }

}

void debugPrintTrackSet(const TrackSet &tracks, const std::vector<Detection> &allDets) 
{
    std::set<Track>::const_iterator trackIter;
    unsigned int trackCount = 0;
    for (trackIter = tracks.componentTracks.begin();
         trackIter != tracks.componentTracks.end();
         trackIter++) {
        std::cout << " track " << trackCount << ":\n";
        std::set<unsigned int>::const_iterator detIdIter;
        for (detIdIter = trackIter->componentDetectionIndices.begin();
             detIdIter != trackIter->componentDetectionIndices.end();
             detIdIter++) {
            std::cout << '\t' << *detIdIter << ": " << allDets.at(*detIdIter).getID() << " "
                      << allDets.at(*detIdIter).getEpochMJD() << 
                " " << allDets.at(*detIdIter).getRA() << " "<< allDets.at(*detIdIter).getDec() << std::endl;
        }
        trackCount++;
    }

}



namespace ctExcept = collapseTracklets::exceptions;


/*
 * generateTrack: create a new ground-truth track with specified
 * sky-plane behavior and specified obstimes.
 *
 * for each inner vector of trackletObsTimes, we will create a tracklet
 * and add it to allTracklets. It will be given detections at times specified
 * by the doubles contained in the vector.
 * 
 * we will ASSUME that the track is expected to have RA0, Dec0 at time
 * trackletObsTimes[0][0].  If trackletObsTimes is in a non-sorted order, expect
 * weird behavior.
 *
 * each detection will be given a new, unique ID > lastDetId and lastDetId will
 * be MODIFIED to be the last, greated detection ID created. allDetections will
 * be MODIFIED and the new detection will be added to it.
 *
 * similarly, allTracklets will be MODIFIED with new tracklets. Each tracklet
 * will have a new, unique ID > lastTrackletID, and lastTrackletId will be
 * MODIFIED to the last, largest ID we assign.
 *
 * Finally, return a Track containing the IDs of all detections and tracklets
 * created.
 * 
 */
Track generateTrack(double ra0, double dec0, double raV, double decV,
                    double raAcc, double decAcc,
                    std::vector<std::vector <double> > trackletObsTimes,
                    std::vector<Detection> &allDetections,
                    std::vector<Tracklet> &allTracklets, 
                    unsigned int & lastDetId,
                    unsigned int & lastTrackletId) {

    if (trackletObsTimes.size() == 0) {
        throw LSST_EXCEPT(ctExcept::BadParameterException, 
                          std::string(__FUNCTION__)+
                          std::string(": cannot build a track with 0 obs times!"));
    }
    if (trackletObsTimes.at(0).size() == 0) {
        throw LSST_EXCEPT(ctExcept::BadParameterException, 
                          std::string(__FUNCTION__)+
                          std::string(": cannot build a tracklet with 0 obs times!"));
    }
    
    double time0 = trackletObsTimes.at(0).at(0);
    std::vector<std::vector <double > >::const_iterator trackletIter;
    std::vector<double>::const_iterator obsTime;

    Track newTrack;
    for(trackletIter = trackletObsTimes.begin();
        trackletIter != trackletObsTimes.end();
        trackletIter++) {
        if (trackletIter->size() == 0) {
            throw LSST_EXCEPT(ctExcept::BadParameterException, 
                              std::string(__FUNCTION__)+
                              std::string(": cannot build tracklet with 0 obs times!"));
        }
        lastTrackletId++;
        Tracklet newTracklet;
        for (obsTime = trackletIter->begin();
             obsTime != trackletIter->end();
             obsTime++) {
            double resultRa = ra0;
            double resultDec = dec0;
            double tempRaV = raV;
            double tempDecV = decV;
            // calculate ra, dec at this obs time.
            double deltaTime = *obsTime - time0;
            modifyWithAcceleration(resultRa,  tempRaV,  raAcc,  deltaTime);
            modifyWithAcceleration(resultDec, tempDecV, decAcc, deltaTime);
            // create new det, add it to our total set of dets,
            // add its ID to the cur tracklet, and cur track.
            lastDetId++;
            Detection newDet(lastDetId, *obsTime, resultRa, resultDec);
            allDetections.push_back(newDet);
            newTracklet.indices.insert(lastDetId);
            newTrack.componentDetectionIndices.insert(lastDetId);           
        }
        allTracklets.push_back(newTracklet);
        if (allTracklets.size() -1 != lastTrackletId) {
            throw LSST_EXCEPT(ctExcept::BadParameterException,
                              std::string(__FUNCTION__)+
                              std::string(": tracklet IDs are assumed to be the index of the tracklet into the tracklet vector."));
        }
        newTrack.componentTrackletIndices.insert(lastTrackletId);
        
    }

    return newTrack;
}





/*
BOOST_AUTO_TEST_CASE( track_1) {
    Track t1;
    Track t2;
    t1.componentDetectionIndices.insert(1);
    t2.componentDetectionIndices.insert(1);
    t1.componentDetectionIndices.insert(2);
    t2.componentDetectionIndices.insert(2);

    t1.componentTrackletIndices.insert(1);
    t2.componentTrackletIndices.insert(1);

    BOOST_CHECK( t1 == t2 );
}


BOOST_AUTO_TEST_CASE( track_2) {
    Track t1;
    Track t2;
    t1.componentDetectionIndices.insert(1);
    t2.componentDetectionIndices.insert(1);
    t1.componentDetectionIndices.insert(2);
    t2.componentDetectionIndices.insert(3);

    t1.componentTrackletIndices.insert(1);
    t2.componentTrackletIndices.insert(2);

    BOOST_CHECK( t1 != t2 );
}


BOOST_AUTO_TEST_CASE( trackSet_1) {
    Track t1;
    Track t2;
    t1.componentDetectionIndices.insert(1);
    t2.componentDetectionIndices.insert(1);
    t1.componentDetectionIndices.insert(2);
    t2.componentDetectionIndices.insert(2);

    t1.componentTrackletIndices.insert(1);
    t2.componentTrackletIndices.insert(1);

    TrackSet ts1;
    TrackSet ts2;
    ts1.insert(t1);
    ts2.insert(t2);

    BOOST_CHECK( ts1 == ts2);
}




BOOST_AUTO_TEST_CASE( trackSet_2) {
    Track t1;
    Track t2;
    t1.componentDetectionIndices.insert(1);
    t2.componentDetectionIndices.insert(1);
    t1.componentDetectionIndices.insert(2);
    t2.componentDetectionIndices.insert(3);

    t1.componentTrackletIndices.insert(1);
    t2.componentTrackletIndices.insert(2);

    TrackSet ts1;
    TrackSet ts2;
    ts1.insert(t1);
    ts2.insert(t2);

    BOOST_CHECK( ts1 != ts2);
}


BOOST_AUTO_TEST_CASE (trackSet_3) {

    Track t1;
    Track t2;
    Track t11;
    Track t22;

    Track t3;

    t1.componentDetectionIndices.insert(1);
    t1.componentDetectionIndices.insert(2);

    t1.componentTrackletIndices.insert(1);


    t2.componentDetectionIndices.insert(3);
    t2.componentDetectionIndices.insert(4);

    t2.componentTrackletIndices.insert(2);


    t11.componentDetectionIndices.insert(10);
    t11.componentDetectionIndices.insert(20);

    t11.componentTrackletIndices.insert(10);


    t22.componentDetectionIndices.insert(30);
    t22.componentDetectionIndices.insert(40);

    t22.componentTrackletIndices.insert(20);


    t3.componentDetectionIndices.insert(4);
    t3.componentDetectionIndices.insert(5);

    t3.componentTrackletIndices.insert(3);

    TrackSet ts1;
    TrackSet ts2;

    ts1.insert(t1);
    ts1.insert(t2);
    BOOST_CHECK(ts1.size() == 2);
    ts2.insert(t1);
    ts2.insert(t2);
    BOOST_CHECK(ts2.size() == 2);
    
    BOOST_CHECK(ts1 == ts2);
    
    ts2.insert(t3);
    BOOST_CHECK(ts2.size() == 3);

    BOOST_CHECK(ts1 != ts2);
    BOOST_CHECK(ts1.isSubsetOf(ts2));

}





BOOST_AUTO_TEST_CASE( linkTracklets_whitebox_getBestFitVelocityAndAcceleration_test0) 
{
    std::vector<double> positions;
    std::vector<double> times;
    positions.push_back(0);
    positions.push_back(2);
    positions.push_back(6);
    times.push_back(0);
    times.push_back(1);
    times.push_back(2);
    double velocity, acceleration, position0;
    getBestFitVelocityAndAcceleration(positions, times, velocity, acceleration, position0);
    //std::cout << "position = " << position0 << " + " << velocity << "*t + " << acceleration << "*t^2" << std::endl;
    BOOST_CHECK(Eq(position0,    0));
    BOOST_CHECK(Eq(velocity,     1));
    BOOST_CHECK(Eq(acceleration, 1));    
}





BOOST_AUTO_TEST_CASE( linkTracklets_whitebox_getBestFitVelocityAndAcceleration_test1) 
{
    std::vector<double> positions;
    std::vector<double> times;
    positions.push_back(2);
    positions.push_back(6);
    positions.push_back(12);
    times.push_back(1);
    times.push_back(2);
    times.push_back(3);
    double velocity, acceleration, position0;
    getBestFitVelocityAndAcceleration(positions, times, velocity, acceleration, position0);
    //std::cout << "position = " << position0 << " + " << velocity << "*t + " << acceleration << "*t^2" << std::endl;
    BOOST_CHECK(Eq(position0,    0));
    BOOST_CHECK(Eq(velocity,     1));
    BOOST_CHECK(Eq(acceleration, 1));    
}
*/





// helper function for creating sets of detections
void addDetectionAt(double MJD, double RA, double dec,  std::vector<Detection> &detVec)
{
    Detection tmpDet(detVec.size(), MJD, RA, dec, 566, "dummy",
                     24.0, 0., 0.);
    detVec.push_back(tmpDet);
}


void addPair(unsigned int id1, unsigned int id2, std::vector<Tracklet> &trackletVec) 
{
    Tracklet tmpTracklet;
    tmpTracklet.indices.insert(id1);
    tmpTracklet.indices.insert(id2);
    trackletVec.push_back(tmpTracklet);
}



int mpi_starter(int *argc, char ***argv)
{
  std::cerr << "Initalizing MPI." << std::endl;
  int requestedLevel = MPI_THREAD_MULTIPLE;
  int providedLevel;
  return MPI_Init_thread(argc, argv, requestedLevel, &providedLevel);
}


BOOST_AUTO_TEST_CASE( startMPI )
{
  int argc;
  char **argv;
  BOOST_CHECK( mpi_starter(&argc, &argv) == MPI_SUCCESS );
}


/************
 *PASSED
 *
BOOST_AUTO_TEST_CASE( linkTracklets_blackbox_1 )
{
  std::cerr << "Starting linktracklets_blackbox_1" << std::endl;
  
  // call with empty dets
  std::vector<Detection> myDets;
  std::vector<Tracklet> pairs;
  linkTrackletsConfig myConfig;
*/
  /**************************************************
   * Distributed implementation specific
   * -set up MPI environment
   **************************************************/

  // Establish MPI-related variable values
  /*int rank, numProcessors;
  MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
  MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
  
  TrackSet results;

  if( rank == 0 ){
    results = linkTracklets(myDets, pairs, myConfig, numProcessors);
    std::cerr << "Master has returned with results size " << results.size() << std::endl;
    BOOST_CHECK(pairs.size() == 0);
  }
  else{
    waitForTask(rank, myDets, pairs, myConfig);
  }

}
  
*/




/************
 *PASSED
 *
BOOST_AUTO_TEST_CASE( linkTracklets_easy_1 )
{
  std::cerr << "Starting linktracklets_easy_1" << std::endl;

  std::vector<Detection> myDets;
  addDetectionAt(5300.0,  50,     50, myDets);
  addDetectionAt(5300.01, 50.001, 50.001, myDets);
  addDetectionAt(5301.0,  50.1,   50.1, myDets);
  addDetectionAt(5301.01, 50.101, 50.101, myDets);
  addDetectionAt(5302.0,  50.2,   50.2, myDets);
  addDetectionAt(5302.01, 50.201, 50.201, myDets);


  std::vector<Tracklet> pairs;
  addPair(0,1, pairs);
  addPair(2,3, pairs);
  addPair(4,5, pairs);
  
  linkTrackletsConfig myConfig;
*/
  /**************************************************
   * Distributed implementation specific
   * -set up MPI environment
   **************************************************/

  // Establish MPI-related variable values
  /*int rank, numProcessors;
  MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
  MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
  
  TrackSet results;

  if( rank == 0 ){
    results = linkTracklets(myDets, pairs, myConfig, numProcessors);
    std::cerr << "easy results: " << results.size() << std::endl;
    BOOST_CHECK(results.size() == 1);
  }
  else{
    waitForTask(rank, myDets, pairs, myConfig);
  }
}
  */
  


/******
 * PASSED
 *

BOOST_AUTO_TEST_CASE( linkTracklets_easy_2 )
{
    // same as 1, but with more tracks (all clearly separated)
  std::cerr << "Starting linktracklets_easy_2" << std::endl;

  std::vector<Detection> myDets;
  std::vector<Tracklet> pairs;
  for (unsigned int i = 0; i < 10; i++) {

      addDetectionAt(5300.0,  50 + i,     50, myDets);
      addDetectionAt(5300.01, 50.001 + i, 50.001, myDets);
      addDetectionAt(5301.0,  50.1 + i,   50.1, myDets);
      addDetectionAt(5301.01, 50.101 + i, 50.101, myDets);
      addDetectionAt(5302.0,  50.2 + i,   50.2, myDets);
      addDetectionAt(5302.01, 50.201 + i, 50.201, myDets);

      addPair(0 + 6*i,1 + 6*i, pairs);
      addPair(2 + 6*i,3 + 6*i, pairs);
      addPair(4 + 6*i,5 + 6*i, pairs);
      
  }

  
  linkTrackletsConfig myConfig;


  // Establish MPI-related variable values
  int rank, numProcessors;
  MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
  MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
  
  TrackSet results;

  if( rank == 0 ){
    results = linkTracklets(myDets, pairs, myConfig, numProcessors);
    std::cerr << "easy_2 results size: " << results.size() << std::endl;
    BOOST_CHECK(results.size() == 10);
  }
  else{
    waitForTask(rank, myDets, pairs, myConfig);
  }
}
*/








/*****
 * PASSED
 *
BOOST_AUTO_TEST_CASE( linkTracklets_easy_3 )
{
    // same as 2, but with tracks crossing RA 0 line
  std::cerr << "Starting linktracklets_easy_3" << std::endl;
  std::vector<Detection> myDets;
  std::vector<Tracklet> pairs;
  for (unsigned int i = 0; i < 10; i++) {

      addDetectionAt(5300.0,  50 + i,     50, myDets);
      addDetectionAt(5300.01, 50.001 + i, 50.001, myDets);
      addDetectionAt(5301.0,  50.1 + i,   50.1, myDets);
      addDetectionAt(5301.01, 50.101 + i, 50.101, myDets);
      addDetectionAt(5302.0,  50.2 + i,   50.2, myDets);
      addDetectionAt(5302.01, 50.201 + i, 50.201, myDets);

      addPair(0 + 6*i,1 + 6*i, pairs);
      addPair(2 + 6*i,3 + 6*i, pairs);
      addPair(4 + 6*i,5 + 6*i, pairs);
      
  }

  
  linkTrackletsConfig myConfig;
*/
  /**************************************************
   * Distributed implementation specific
   * -set up MPI environment
   *************************************************/

  // Establish MPI-related variable values
  /*int rank, numProcessors;
  MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
  MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
  
  TrackSet results;

  if( rank == 0 ){
    results = linkTracklets(myDets, pairs, myConfig, numProcessors);
    BOOST_CHECK(results.size() == 10);
  }
  else{
    waitForTask(rank, myDets, pairs, myConfig);
  }

}
*/





  /******
   *PASSED
   *
BOOST_AUTO_TEST_CASE( linkTracklets_easy_4_1 )
{
  // same as 1, but with track crossing RA 0 line
  std::cerr << "Starting linktracklets_easy_4_1" << std::endl;
  std::vector<Detection> myDets;
  std::vector<Tracklet> pairs;

  addDetectionAt(5300.0,  359.9,       50, myDets);
  addDetectionAt(5300.01, 359.901, 50.001, myDets);
  addDetectionAt(5301.0,  0.,        50.1, myDets);
  addDetectionAt(5301.01, 0.001,    50.101, myDets);
  addDetectionAt(5302.0,   0.1,      50.2, myDets);
  addDetectionAt(5302.01,  0.101,  50.201, myDets);
  
  addPair(0,1, pairs);
  addPair(2,3, pairs);
  addPair(4,5, pairs);
  
  linkTrackletsConfig myConfig;
  */
  /**************************************************
   * Distributed implementation specific
   * -set up MPI environment
   *************************************************/

  // Establish MPI-related variable values
    /*int rank, numProcessors;
  MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
  MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
  
  TrackSet results;

  if( rank == 0 ){
    results = linkTracklets(myDets, pairs, myConfig, numProcessors);
    BOOST_CHECK(results.size() == 1);
  }
  else{
    waitForTask(rank, myDets, pairs, myConfig);
  }
}
  
    */


    /*****
     * PASSED
     *
BOOST_AUTO_TEST_CASE( linkTracklets_easy_4 )
{
    // same as 2, but with tracks crossing RA 0 line

  std::vector<Detection> myDets;
  std::vector<Tracklet> pairs;
  for (unsigned int i = 0; i < 10; i++) {

      addDetectionAt(5300.0,  359.9,       50 + i, myDets);
      addDetectionAt(5300.01, 359.901, 50.001 + i, myDets);
      addDetectionAt(5301.0,  0.,        50.1 + i, myDets);
      addDetectionAt(5301.01, 0.001,   50.101 + i, myDets);
      addDetectionAt(5302.0,   0.1,      50.2 + i, myDets);
      addDetectionAt(5302.01,  0.101,  50.201 + i, myDets);

      addPair(0 + 6*i,1 + 6*i, pairs);
      addPair(2 + 6*i,3 + 6*i, pairs);
      addPair(4 + 6*i,5 + 6*i, pairs);
      
  }

  
  linkTrackletsConfig myConfig;
    */

  /**************************************************
   * Distributed implementation specific
   * -set up MPI environment
   *************************************************/

  // Establish MPI-related variable values
  /*int rank, numProcessors;
  MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
  MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
  
  TrackSet results;

  if( rank == 0 ){
    results = linkTracklets(myDets, pairs, myConfig, numProcessors);
    BOOST_CHECK(results.size() == 10);
  }
  else{
    waitForTask(rank, myDets, pairs, myConfig);
  }
}
    */




    /*****
     * PASSED
     *
BOOST_AUTO_TEST_CASE( linkTracklets_easy_5 )
{
    // same as 4, but going the other way!

  std::vector<Detection> myDets;
  std::vector<Tracklet> pairs;
  for (unsigned int i = 0; i < 10; i++) {

      addDetectionAt(5300.0,    0.101,    50.201 + i, myDets);
      addDetectionAt(5300.01,   0.1,      50.2   + i, myDets);

      addDetectionAt(5301.0,    0.001,    50.101 + i, myDets);
      addDetectionAt(5301.01,   0.,       50.1   + i, myDets);

      addDetectionAt(5302.0,  359.901,    50.001 + i, myDets);
      addDetectionAt(5302.01, 359.9,      50.    + i, myDets);

      addPair(0 + 6*i,   1 + 6*i,   pairs);
      addPair(2 + 6*i,   3 + 6*i,   pairs);
      addPair(4 + 6*i,   5 + 6*i,   pairs);
      
  }

  
  linkTrackletsConfig myConfig;
    
*/
  /**************************************************
   * Distributed implementation specific
   * -set up MPI environment
   *************************************************/

  // Establish MPI-related variable values
  /*int rank, numProcessors;
  MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
  MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
  
  TrackSet results;

  if( rank == 0 ){
    results = linkTracklets(myDets, pairs, myConfig, numProcessors);
    BOOST_CHECK(results.size() == 10);
  }
  else{
    waitForTask(rank, myDets, pairs, myConfig);
  }


  //std::cout << "results size = " << results.size() << '\n';
  // for (unsigned int i = 0; i < results.size(); i++) {
      
  //     std::set<unsigned int>::const_iterator dIter;
  //     const std::set<unsigned int> * cur = &(results.at(i).componentDetectionIndices);

  //     for (dIter = cur->begin(); dIter != cur->end(); dIter++) {
  //         std::cout << " " << *dIter;
  //     }
  //     std::cout << "\n";
  //     cur = & ( results.at(i).componentTrackletIndices);
  //     for (dIter = cur->begin(); dIter != cur->end(); dIter++) {
  //         std::cout << " " << *dIter;
  //     }
  //     std::cout << "\n";

  // }


}
    */





    /*****
     *PASSED
     *
// Track generateTrack(double ra0, double dec0, double raV, double decV,
//                     double raAcc, double decAcc,
//                     std::vector<std::vector <double> > trackletObsTimes,
//                     std::vector<Detection> &allDetections,
//                     std::vector<Tracklet> &allTracklets, 
//                     unsigned int & lastDetId,
//                     unsigned int & lastTrackletId) {

BOOST_AUTO_TEST_CASE( linkTracklets_1 )
{
    TrackSet expectedTracks;
    std::vector<Detection> allDets;
    std::vector<Tracklet> allTracklets;
    unsigned int firstDetId = -1;
    unsigned int firstTrackletId = -1;

  
    linkTrackletsConfig myConfig;

    std::vector<std::vector<double> > imgTimes(3);

    imgTimes.at(0).push_back(5300);
    imgTimes.at(0).push_back(5300.03);

    imgTimes.at(1).push_back(5301);
    imgTimes.at(1).push_back(5301.03);

    imgTimes.at(2).push_back(5302);
    imgTimes.at(2).push_back(5302.03);

    expectedTracks.insert(generateTrack(20., 20., 
                                        .25, .01, 
                                        .0002, .002,
                                        imgTimes, 
                                        allDets, allTracklets, 
                                        firstDetId, firstTrackletId));
    */
    /**************************************************
     * Distributed implementation specific
     * -set up MPI environment
     *************************************************/
    
    // Establish MPI-related variable values
      /*int rank, numProcessors;
    MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
    
    TrackSet foundTracks;

    if( rank == 0 ){
      foundTracks = linkTracklets(allDets, allTracklets, myConfig, numProcessors);
      std::cerr << "Expected track is size " << expectedTracks.size() << std::endl;
      std::cerr << "Found track is sisze " << foundTracks.size() << std::endl;
      BOOST_CHECK(foundTracks == expectedTracks);
    }
    else{
      waitForTask(rank, allDets, allTracklets, myConfig);
    }
}
      */


      /*****
       *PASSED
       *
BOOST_AUTO_TEST_CASE( linkTracklets_2 )
{
    // lots of support nodes!
    TrackSet expectedTracks;
    std::vector<Detection> allDets;
    std::vector<Tracklet> allTracklets;
    unsigned int firstDetId = -1;
    unsigned int firstTrackletId = -1;

  
    linkTrackletsConfig myConfig;

    std::vector<std::vector<double> > imgTimes(7);

    imgTimes.at(0).push_back(5300);
    imgTimes.at(0).push_back(5300.03);

    imgTimes.at(1).push_back(5301);
    imgTimes.at(1).push_back(5301.03);

    imgTimes.at(2).push_back(5302);
    imgTimes.at(2).push_back(5302.03);

    imgTimes.at(3).push_back(5303);
    imgTimes.at(3).push_back(5303.03);

    imgTimes.at(4).push_back(5304);
    imgTimes.at(4).push_back(5304.03);

    imgTimes.at(5).push_back(5305);
    imgTimes.at(5).push_back(5305.03);

    imgTimes.at(6).push_back(5306);
    imgTimes.at(6).push_back(5306.03);

    expectedTracks.insert(generateTrack(20., 20., 
                                        .25, .01, 
                                        .0002, .002,
                                        imgTimes, 
                                        allDets, allTracklets, 
                                        firstDetId, firstTrackletId));
    
      */
    
    /**************************************************
     * Distributed implementation specific
     * -set up MPI environment
     *************************************************/
    
    // Establish MPI-related variable values
    /*int rank, numProcessors;
    MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
    
    TrackSet foundTracks;

    if( rank == 0 ){
      foundTracks = linkTracklets(allDets, allTracklets, myConfig, numProcessors);
      
      std::cout << "checking linkTracklets results; size:" << foundTracks.size() << std::endl;
      BOOST_CHECK(expectedTracks.isSubsetOf(foundTracks));
    }
    else{
      waitForTask(rank, allDets, allTracklets, myConfig);
    }
}
    */



      /*****
       *PASSED
       *

BOOST_AUTO_TEST_CASE( linkTracklets_3 )
{
    // lots of support nodes, and a psuedo-deep stack
    TrackSet expectedTracks;
    std::vector<Detection> allDets;
    std::vector<Tracklet> allTracklets;
    unsigned int firstDetId = -1;
    unsigned int firstTrackletId = -1;

  
    linkTrackletsConfig myConfig;

    std::vector<std::vector<double> > imgTimes(7);

    imgTimes.at(0).push_back(5300);
    imgTimes.at(0).push_back(5300.03);

    imgTimes.at(1).push_back(5301);
    imgTimes.at(1).push_back(5301.03);

    imgTimes.at(2).push_back(5302);
    imgTimes.at(2).push_back(5302.03);

    imgTimes.at(3).push_back(5303);
    imgTimes.at(3).push_back(5303.005);    
    imgTimes.at(3).push_back(5303.01);
    imgTimes.at(3).push_back(5303.015);
    imgTimes.at(3).push_back(5303.02);
    imgTimes.at(3).push_back(5303.025);
    imgTimes.at(3).push_back(5303.03);
    imgTimes.at(3).push_back(5303.035);
    imgTimes.at(3).push_back(5303.04);
    imgTimes.at(3).push_back(5303.045);
    imgTimes.at(3).push_back(5303.05);
    imgTimes.at(3).push_back(5303.055);

    imgTimes.at(4).push_back(5304);
    imgTimes.at(4).push_back(5304.03);

    imgTimes.at(5).push_back(5305);
    imgTimes.at(5).push_back(5305.03);

    imgTimes.at(6).push_back(5306);
    imgTimes.at(6).push_back(5306.03);

    expectedTracks.insert(generateTrack(20., 20., 
                                        .25, .01, 
                                        .0002, .002,
                                        imgTimes, 
                                        allDets, allTracklets, 
                                        firstDetId, firstTrackletId));

*/
    /**************************************************
     * Distributed implementation specific
     * -set up MPI environment
     *************************************************/
    
    // Establish MPI-related variable values
    /*int rank, numProcessors;
    MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
    
    TrackSet foundTracks;

    if( rank == 0 ){
      foundTracks = linkTracklets(allDets, allTracklets, myConfig, numProcessors);
      BOOST_CHECK(expectedTracks.isSubsetOf(foundTracks));
    }
    else{
      waitForTask(rank, allDets, allTracklets, myConfig);
    }

    // std::cout << " got tracks: \n";
    // debugPrintTrackSet(foundTracks, allDets);
    // std::cout << " expected tracks: \n";
    // debugPrintTrackSet(expectedTracks, allDets);
}
*/




      /*****
       *PASSED
       *


BOOST_AUTO_TEST_CASE( linkTracklets_4 )
{

    // lots of tracks this time. still simple cadence.
    TrackSet expectedTracks;
    std::vector<Detection> allDets;
    std::vector<Tracklet> allTracklets;
    unsigned int firstDetId = -1;
    unsigned int firstTrackletId = -1;

  
    linkTrackletsConfig myConfig;

    std::vector<std::vector<double> > imgTimes(3);

    imgTimes.at(0).push_back(5300);
    imgTimes.at(0).push_back(5300.03);

    imgTimes.at(1).push_back(5301);
    imgTimes.at(1).push_back(5301.03);

    imgTimes.at(2).push_back(5302);
    imgTimes.at(2).push_back(5302.03);

    expectedTracks.insert(generateTrack(20., 20., 
                                        .25, .01, 
                                        .0002, .002,
                                        imgTimes, 
                                        allDets, allTracklets, 
                                        firstDetId, firstTrackletId));

    expectedTracks.insert(generateTrack(10., 10., 
                                        -.02, .015, 
                                        .00025, .0002,
                                        imgTimes, 
                                        allDets, allTracklets, 
                                        firstDetId, firstTrackletId));

    expectedTracks.insert(generateTrack(15., 10., 
                                        .001, .00001, 
                                        .00015, .000023,
                                        imgTimes, 
                                        allDets, allTracklets, 
                                        firstDetId, firstTrackletId));

    expectedTracks.insert(generateTrack(12.5, 12.5, 
                                        -.01, -.001, 
                                        .000001, -.00023,
                                        imgTimes, 
                                        allDets, allTracklets, 
                                        firstDetId, firstTrackletId));

    expectedTracks.insert(generateTrack(13, 14., 
                                        .001, -.01, 
                                        -.001, -.00023,
                                        imgTimes, 
                                        allDets, allTracklets, 
                                        firstDetId, firstTrackletId));

    expectedTracks.insert(generateTrack(14.5, 14., 
                                        .01, -.000001, 
                                        -.00015, .00023,
                                        imgTimes, 
                                        allDets, allTracklets, 
                                        firstDetId, firstTrackletId));

    expectedTracks.insert(generateTrack(16.5, 14., 
                                        .0155, .000001, 
                                        -.00015, -.00023,
                                        imgTimes, 
                                        allDets, allTracklets, 
                                        firstDetId, firstTrackletId));

    expectedTracks.insert(generateTrack(10.5, 13., 
                                        .000155, .0001, 
                                        .00066, -.00066,
                                        imgTimes, 
                                        allDets, allTracklets, 
                                        firstDetId, firstTrackletId));

    expectedTracks.insert(generateTrack(17.5, 12.5, 
                                        -.011, -.001, 
                                        .0001112, -.0002388,
                                        imgTimes, 
                                        allDets, allTracklets, 
                                        firstDetId, firstTrackletId));


    expectedTracks.insert(generateTrack(12, 19.5, 
                                        .001333, -.008888, 
                                        -.0039083, -.001999,
                                        imgTimes, 
                                        allDets, allTracklets, 
                                        firstDetId, firstTrackletId));

    */
    /**************************************************
     * Distributed implementation specific
     * -set up MPI environment
     *************************************************/
    
    // Establish MPI-related variable values
    /*int rank, numProcessors;
    MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
    
    TrackSet foundTracks;

    if( rank == 0 ){
      foundTracks = linkTracklets(allDets, allTracklets, myConfig, numProcessors);
      BOOST_CHECK(foundTracks == expectedTracks);
    }
    else{
      waitForTask(rank, allDets, allTracklets, myConfig);
    }
}
      */







      /*****
       *NOT PASSED
       */
BOOST_AUTO_TEST_CASE( linkTracklets_4_pt_5 )
{

    // lots of tracks, following a coherent pattern but randomly perturbed.
    TrackSet expectedTracks;
    std::vector<Detection> allDets;
    std::vector<Tracklet> allTracklets;
    unsigned int firstDetId = -1;
    unsigned int firstTrackletId = -1;

  
    linkTrackletsConfig myConfig;

    std::vector<std::vector<double> > imgTimes(3);

    imgTimes.at(0).push_back(5300);
    imgTimes.at(0).push_back(5300.03);

    imgTimes.at(1).push_back(5305);
    imgTimes.at(1).push_back(5305.03);

    imgTimes.at(2).push_back(5312);
    imgTimes.at(2).push_back(5312.03);

    // seed the random number generator with a known value;
    // this way the test will be identical on each run.
    srand(2); // srand(1);

    for (unsigned int i = 0; i < 10; i++) {

        // get 6 floating point numbers between 0 and 1.
        std::vector<double> someRands;         
        for (unsigned int j = 0; j < 6; j++) {
            someRands.push_back( (double) rand() / RAND_MAX );
        }
        //generate a random permutation on this track.
        expectedTracks.insert(generateTrack(20. + someRands[0] * 10., //location 
                                            20. + someRands[1] * 10., //location
                                            (someRands[2] - .1) * 2., //1 in 10 chance of retrograde, maxv 2
                                            (someRands[3] - .5) * .5, // maxv .5 in dec 
                                            (someRands[4]) * .0019, //max acc of .0019, always positive
                                            (someRands[5]) * .0019, //same
                                            imgTimes, 
                                            allDets, allTracklets, 
                                            firstDetId, firstTrackletId));
    }

    struct tm * timeinfo;
    time_t rawtime;
    
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );                    
    
    std::cout << "Generated " << expectedTracks.size() << " tracks. Calling linkTracklets ";
    std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;



    /**************************************************
     * Distributed implementation specific
     * -set up MPI environment
     *************************************************/
    
    // Establish MPI-related variable values
    int rank, numProcessors;
    MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
    
    TrackSet foundTracks;

    if( rank == 0 ){
      foundTracks = linkTracklets(allDets, allTracklets, myConfig, numProcessors);
      std::cerr << "FounTracks size is " << foundTracks.size() << std::endl;
      BOOST_CHECK(expectedTracks.isSubsetOf(foundTracks));
    }
    else{
      waitForTask(rank, allDets, allTracklets, myConfig);
      }
    /*
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );                    
    
    std::cout << "got " << foundTracks.size() << " results, checking if they contain the true tracks ";
    std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;
    */

    // std::cout << " got tracks: \n";
    // debugPrintTrackSet(foundTracks, allDets);
    // std::cout << " expected tracks: \n";
    // debugPrintTrackSet(expectedTracks, allDets);
      
}
      
      



      /*****
       * PASSED
       *
BOOST_AUTO_TEST_CASE( linkTracklets_5 )
{

  // " << expectedTracks.size() << " tracks, following a coherent pattern but randomly perturbed.
    TrackSet expectedTracks;
    std::vector<Detection> allDets;
    std::vector<Tracklet> allTracklets;
    unsigned int firstDetId = -1;
    unsigned int firstTrackletId = -1;

  
    linkTrackletsConfig myConfig;

    std::vector<std::vector<double> > imgTimes(3);

    imgTimes.at(0).push_back(5300);
    imgTimes.at(0).push_back(5300.03);

    imgTimes.at(1).push_back(5305);
    imgTimes.at(1).push_back(5305.03);

    imgTimes.at(2).push_back(5312);
    imgTimes.at(2).push_back(5312.03);

    // seed the random number generator with a known value;
    // this way the test will be identical on each run.
    srand(1);

    for (unsigned int i = 0; i < 100; i++) {

        // get 6 floating point numbers between 0 and 1.
        std::vector<double> someRands;         
        for (unsigned int j = 0; j < 6; j++) {
            someRands.push_back( (double) rand() / RAND_MAX );
        }
        //generate a random permutation on this track.
        expectedTracks.insert(generateTrack(20. + someRands[0] * 10., //location 
                                            20. + someRands[1] * 10., //location
                                            (someRands[2] - .1) * 2., //1 in 10 chance of retrograde, maxv 2
                                            (someRands[3] - .5) * .5, // maxv .5 in dec 
                                            (someRands[4]) * .0019, //max acc of .0019, always positive
                                            (someRands[5]) * .0019, //same
                                            imgTimes, 
                                            allDets, allTracklets, 
                                            firstDetId, firstTrackletId));

    }

    struct tm * timeinfo;
    time_t rawtime;
    
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );                    
    
    std::cout << "Generated " << expectedTracks.size() << " tracks. Calling linkTracklets ";
    std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;
     
*/
    /**************************************************
     * Distributed implementation specific
     * -set up MPI environment
     *************************************************/
    
    // Establish MPI-related variable values
    /*std::cerr << "Initalizing MPI." << std::endl;
    int requestedLevel = MPI_THREAD_MULTIPLE;
    int providedLevel;
    MPI_Init_thread(0, 0, requestedLevel, &providedLevel);*/
    

      /*int rank, numProcessors;
    MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
    
    TrackSet foundTracks;

    if( rank == 0 ){
      foundTracks = linkTracklets(allDets, allTracklets, myConfig, numProcessors);
      BOOST_CHECK(expectedTracks.isSubsetOf(foundTracks));
    }
    else{
      waitForTask(rank, allDets, allTracklets, myConfig);
      }*/
    
    /*int rc = MPI_Finalize();
      std::cerr << "Finalized with return code " << rc << std::endl;
    */
    /*
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );                    
    
    std::cout << "got " << foundTracks.size() << " results, checking if they contain the true tracks ";
    std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;
    */
      /*
}
      
      */

      /*****
       * PASSED
       *
BOOST_AUTO_TEST_CASE( linkTracklets_5_1 )
{

    // " << expectedTracks.size() << " tracks, following a coherent pattern but randomly perturbed.
    TrackSet expectedTracks;
    std::vector<Detection> allDets;
    std::vector<Tracklet> allTracklets;
    unsigned int firstDetId = -1;
    unsigned int firstTrackletId = -1;

  
    linkTrackletsConfig myConfig;

    std::vector<std::vector<double> > imgTimes(3);

    imgTimes.at(0).push_back(5300);
    imgTimes.at(0).push_back(5300.03);

    imgTimes.at(1).push_back(5305);
    imgTimes.at(1).push_back(5305.03);

    imgTimes.at(2).push_back(5312);
    imgTimes.at(2).push_back(5312.03);

    // seed the random number generator with a known value;
    // this way the test will be identical on each run.
    srand(2);

    for (unsigned int i = 0; i < 200; i++) {

        // get 6 floating point numbers between 0 and 1.
        std::vector<double> someRands;         
        for (unsigned int j = 0; j < 6; j++) {
            someRands.push_back( (double) rand() / RAND_MAX );
        }
        //generate a random permutation on this track.
        expectedTracks.insert(generateTrack(20. + someRands[0] * 10., //location 
                                            20. + someRands[1] * 10., //location
                                            (someRands[2] - .1) * 2., //1 in 10 chance of retrograde, maxv 2
                                            (someRands[3] - .5) * .5, // maxv .5 in dec 
                                            (someRands[4]) * .0019, //max acc of .0019, always positive
                                            (someRands[5]) * .0019, //same
                                            imgTimes, 
                                            allDets, allTracklets, 
                                            firstDetId, firstTrackletId));

    }

    struct tm * timeinfo;
    time_t rawtime;
    
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );                    
    
    std::cout << "Generated " << expectedTracks.size() << " tracks. Calling linkTracklets ";
    std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;
*/
    /**************************************************
     * Distributed implementation specific
     * -set up MPI environment
     *************************************************/
    
    // Establish MPI-related variable values
      /*int rank, numProcessors;
    MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
    
    TrackSet foundTracks;

    if( rank == 0 ){
      foundTracks = linkTracklets(allDets, allTracklets, myConfig, numProcessors);
      BOOST_CHECK(expectedTracks.isSubsetOf(foundTracks));
    }
    else{
      waitForTask(rank, allDets, allTracklets, myConfig);
      }*/
    /*
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );                    
    
    std::cout << "got " << foundTracks.size() << " results, checking if they contain the true tracks ";
    std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;
    */
      /*      }*/




      /*****
       * PASSED
       *
BOOST_AUTO_TEST_CASE( linkTracklets_5_2 )
{

    // " << expectedTracks.size() << " tracks, following a coherent pattern but randomly perturbed.
    TrackSet expectedTracks;
    std::vector<Detection> allDets;
    std::vector<Tracklet> allTracklets;
    unsigned int firstDetId = -1;
    unsigned int firstTrackletId = -1;

  
    linkTrackletsConfig myConfig;

    std::vector<std::vector<double> > imgTimes(3);

    imgTimes.at(0).push_back(5300);
    imgTimes.at(0).push_back(5300.03);

    imgTimes.at(1).push_back(5305);
    imgTimes.at(1).push_back(5305.03);

    imgTimes.at(2).push_back(5312);
    imgTimes.at(2).push_back(5312.03);

    // seed the random number generator with a known value;
    // this way the test will be identical on each run.
    srand(3);

    for (unsigned int i = 0; i < 300; i++) {

        // get 6 floating point numbers between 0 and 1.
        std::vector<double> someRands;         
        for (unsigned int j = 0; j < 6; j++) {
            someRands.push_back( (double) rand() / RAND_MAX );
        }
        //generate a random permutation on this track.
        expectedTracks.insert(generateTrack(20. + someRands[0] * 10., //location 
                                            20. + someRands[1] * 10., //location
                                            (someRands[2] - .1) * 2., //1 in 10 chance of retrograde, maxv 2
                                            (someRands[3] - .5) * .5, // maxv .5 in dec 
                                            (someRands[4]) * .0019, //max acc of .0019, always positive
                                            (someRands[5]) * .0019, //same
                                            imgTimes, 
                                            allDets, allTracklets, 
                                            firstDetId, firstTrackletId));

    }

    struct tm * timeinfo;
    time_t rawtime;
    
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );                    
    
    std::cout << "(5_2) Generated " << expectedTracks.size() << " tracks. Calling linkTracklets ";
    std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;
      */
    /**************************************************
     * Distributed implementation specific
     * -set up MPI environment
     *************************************************/
    
    // Establish MPI-related variable values
      /*int rank, numProcessors;
    MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
    
    TrackSet foundTracks;

    if( rank == 0 ){
      foundTracks = linkTracklets(allDets, allTracklets, myConfig, numProcessors);
      BOOST_CHECK(expectedTracks.isSubsetOf(foundTracks));
    }
    else{
      waitForTask(rank, allDets, allTracklets, myConfig);
    }
      */
    /*
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );                    
    
    std::cout << "got " << foundTracks.size() << " results, checking if they contain the true tracks ";
    std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;
    */
      /*       }*/




      /*****
       * PASSED
       *
BOOST_AUTO_TEST_CASE( linkTracklets_5_3 )
{

  // " << expectedTracks.size() << " tracks, following a coherent pattern but randomly perturbed.
    TrackSet expectedTracks;
    std::vector<Detection> allDets;
    std::vector<Tracklet> allTracklets;
    unsigned int firstDetId = -1;
    unsigned int firstTrackletId = -1;

  
    linkTrackletsConfig myConfig;

    std::vector<std::vector<double> > imgTimes(3);

    imgTimes.at(0).push_back(5300);
    imgTimes.at(0).push_back(5300.03);

    imgTimes.at(1).push_back(5305);
    imgTimes.at(1).push_back(5305.03);

    imgTimes.at(2).push_back(5312);
    imgTimes.at(2).push_back(5312.03);

    // seed the random number generator with a known value;
    // this way the test will be identical on each run.
    srand(4);

    for (unsigned int i = 0; i < 400; i++) {

        // get 6 floating point numbers between 0 and 1.
        std::vector<double> someRands;         
        for (unsigned int j = 0; j < 6; j++) {
            someRands.push_back( (double) rand() / RAND_MAX );
        }
        //generate a random permutation on this track.
        expectedTracks.insert(generateTrack(20. + someRands[0] * 10., //location 
                                            20. + someRands[1] * 10., //location
                                            (someRands[2] - .1) * 2., //1 in 10 chance of retrograde, maxv 2
                                            (someRands[3] - .5) * .5, // maxv .5 in dec 
                                            (someRands[4]) * .0019, //max acc of .0019, always positive
                                            (someRands[5]) * .0019, //same
                                            imgTimes, 
                                            allDets, allTracklets, 
                                            firstDetId, firstTrackletId));

    }

    struct tm * timeinfo;
    time_t rawtime;
    
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );                    
    
    std::cout << "Generated " << expectedTracks.size() << " tracks. Calling linkTracklets ";
    std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;
      */
    /**************************************************
     * Distributed implementation specific
     * -set up MPI environment
     *************************************************/
    
    // Establish MPI-related variable values
      /*int rank, numProcessors;
    MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
    
    TrackSet foundTracks;

    if( rank == 0 ){
      foundTracks = linkTracklets(allDets, allTracklets, myConfig, numProcessors);
      BOOST_CHECK(expectedTracks.isSubsetOf(foundTracks));
    }
    else{
      waitForTask(rank, allDets, allTracklets, myConfig);
    }
      */
    /*
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );                    
    
    std::cout << "got " << foundTracks.size() << " results, checking if they contain the true tracks ";
    std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;
    */

      /*       }*/



      /*****
       * PASSED -- TODO mpi_finalize hung
       *
BOOST_AUTO_TEST_CASE( linkTracklets_5_4 )
{

    // " << expectedTracks.size() << " tracks, following a coherent pattern but randomly perturbed.
    TrackSet expectedTracks;
    std::vector<Detection> allDets;
    std::vector<Tracklet> allTracklets;
    unsigned int firstDetId = -1;
    unsigned int firstTrackletId = -1;

  
    linkTrackletsConfig myConfig;

    std::vector<std::vector<double> > imgTimes(3);

    imgTimes.at(0).push_back(5300);
    imgTimes.at(0).push_back(5300.03);

    imgTimes.at(1).push_back(5305);
    imgTimes.at(1).push_back(5305.03);

    imgTimes.at(2).push_back(5312);
    imgTimes.at(2).push_back(5312.03);

    // seed the random number generator with a known value;
    // this way the test will be identical on each run.
    srand(5);

    for (unsigned int i = 0; i < 500; i++) {

        // get 6 floating point numbers between 0 and 1.
        std::vector<double> someRands;         
        for (unsigned int j = 0; j < 6; j++) {
            someRands.push_back( (double) rand() / RAND_MAX );
        }
        //generate a random permutation on this track.
        expectedTracks.insert(generateTrack(20. + someRands[0] * 10., //location 
                                            20. + someRands[1] * 10., //location
                                            (someRands[2] - .1) * 2., //1 in 10 chance of retrograde, maxv 2
                                            (someRands[3] - .5) * .5, // maxv .5 in dec 
                                            (someRands[4]) * .0019, //max acc of .0019, always positive
                                            (someRands[5]) * .0019, //same
                                            imgTimes, 
                                            allDets, allTracklets, 
                                            firstDetId, firstTrackletId));

    }

    struct tm * timeinfo;
    time_t rawtime;
    
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );                    
    
    std::cout << "Generated " << expectedTracks.size() << " tracks. Calling linkTracklets ";
    std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;
      
*/
    /**************************************************
     * Distributed implementation specific
     * -set up MPI environment
     *************************************************/
    
    // Establish MPI-related variable values
      /*  int rank, numProcessors;
    MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
    
    TrackSet foundTracks;

    if( rank == 0 ){
      foundTracks = linkTracklets(allDets, allTracklets, myConfig, numProcessors);
      //BOOST_CHECK(expectedTracks.isSubsetOf(foundTracks));
    }
    else{
      waitForTask(rank, allDets, allTracklets, myConfig);
      }
      */
    /*
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );                    
    
    std::cout << "got " << foundTracks.size() << " results, checking if they contain the true tracks ";
    std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;
    */
      /*}*/



      /*****
       * PASSED
       *
      BOOST_AUTO_TEST_CASE( linkTracklets_5_5 )
{

    // " << expectedTracks.size() << " tracks, following a coherent pattern but randomly perturbed.
    TrackSet expectedTracks;
    std::vector<Detection> allDets;
    std::vector<Tracklet> allTracklets;
    unsigned int firstDetId = -1;
    unsigned int firstTrackletId = -1;

  
    linkTrackletsConfig myConfig;

    std::vector<std::vector<double> > imgTimes(3);

    imgTimes.at(0).push_back(5300);
    imgTimes.at(0).push_back(5300.03);

    imgTimes.at(1).push_back(5305);
    imgTimes.at(1).push_back(5305.03);

    imgTimes.at(2).push_back(5312);
    imgTimes.at(2).push_back(5312.03);

    // seed the random number generator with a known value;
    // this way the test will be identical on each run.
    srand(6);
    
      for (unsigned int i = 0; i < 1200; i++) {
      
        // get 6 floating point numbers between 0 and 1.
        std::vector<double> someRands;         
        for (unsigned int j = 0; j < 6; j++) {
            someRands.push_back( (double) rand() / RAND_MAX );
        }
        //generate a random permutation on this track.
        expectedTracks.insert(generateTrack(20. + someRands[0] * 10., //location 
                                            20. + someRands[1] * 10., //location
                                            (someRands[2] - .1) * 2., //1 in 10 chance of retrograde, maxv 2
                                            (someRands[3] - .5) * .5, // maxv .5 in dec 
                                            (someRands[4]) * .0019, //max acc of .0019, always positive
                                            (someRands[5]) * .0019, //same
                                            imgTimes, 
                                            allDets, allTracklets, 
                                            firstDetId, firstTrackletId));

    }

    struct tm * timeinfo;
    time_t rawtime;
    
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );                    
    
    std::cout << "Generated " << expectedTracks.size() << " tracks. Calling linkTracklets ";
    std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;
      */
    /**************************************************
     * Distributed implementation specific
     * -set up MPI environment
     *************************************************/
    
    // Establish MPI-related variable values
    /*int rank, numProcessors;
    MPI_Comm_size(MPI_COMM_WORLD, &numProcessors); 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
    
    TrackSet foundTracks;
    if( rank == 0 ){
      foundTracks = linkTracklets(allDets, allTracklets, myConfig, numProcessors);
      BOOST_CHECK(expectedTracks.isSubsetOf(foundTracks));
    }
    else{
      waitForTask(rank, allDets, allTracklets, myConfig);
    }
    */
    /*
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );                    
    
    std::cout << "got " << foundTracks.size() << " results, checking if they contain the true tracks ";
    std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;
    */
    /*}*/



















int mpi_stopper()
{
  std::cerr << "Finalizing MPI" << std::endl;
  return MPI_Finalize();
}


BOOST_AUTO_TEST_CASE( stopMPI )
{
  std::cout << "checking MPI results" << std::endl;
  BOOST_CHECK( mpi_stopper() == MPI_SUCCESS );
}


// TBD: check that tracks with too-high acceleration are correctly rejected, etc.
