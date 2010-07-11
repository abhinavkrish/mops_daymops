// -*- LSST-C++ -*-

/* 
 * LSST Data Management System
 * Copyright 2008, 2009, 2010 LSST Corporation.
 * 
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the LSST License Statement and 
 * the GNU General Public License along with this program.  If not, 
 * see <http://www.lsstcorp.org/LegalNotices/>.
 */
 
/* jonathan myers */

#include <iomanip>
#include <map>
#include <gsl/gsl_multifit.h>
#include <time.h>

#include "linkTracklets.h"
#include "../rmsLineFit.h"
#include "../Exceptions.h"
#include "../KDTree.h"


//#define LEAF_SIZE 1024
//#define LEAF_SIZE 256
#define LEAF_SIZE 2



/* taking a queue from Kubica, it's only once per ITERATIONS_PER_SPLIT calls to
 * doLinkingRecurse that we actually split the (non-leaf) support nodes. The
 * idea is to avoid redundantly calculating whether a set of support nodes is
 * compatible.
 */
#define ITERATIONS_PER_SPLIT 0

/* 
the following flags, if set to 'true, will enable some debugging checks which
use brute-force searching and ground truth data to alert the user if an object
is going to be missed.  (not foolproof, but pretty good).  Will make searching
INSANELY slow.
 */
#define CHEAT_AND_DO_CORRECTNESS_CHECKS_AT_SUPPORT_POINTS false
#define CHEAT_AND_DO_CORRECTNESS_CHECKS_BY_ENDPOINT false
#define CHEAT_AND_REPORT_FINDABLE_OBJECTS false
#define CHEAT_AND_DO_TOP_LEVEL_CHECK false


#define POINT_RA           0
#define POINT_DEC          1
#define POINT_RA_VELOCITY   2
#define POINT_DEC_VELOCITY  3




double getAllDetectionsForTrackletTime, getFirstDetectionForTrackletTime, 
    modifyWithAccelerationTime, positionAndVelocityRangesOverlapAfterAccelerationTime, 
    areCompatibleTime, getBestFitVelocityAndAccelerationTime,
    getBestFitVelocityAndAccelerationForTrackletsTime, addBestCompatibleTrackletsAndDetectionsToTrackTime, 
    trackMeetsRequirementsTime, endpointTrackletsAreCompatibleTime, buildTracksAddToResultsTime, 
    areAllLeavesTime, nodeWidthTime, addAllDetectedObjectsToSetTime, doLinkingRecurseTime;

int doLinkingRecurseVisits, buildTracksAddToResultsVisits, compatibleEndpointsFound;

namespace ctExcept = collapseTracklets::exceptions;


// TBD: REMOVE
// THESE ARE FOR DEBUGGING ONLY
#include <ctime>
double getTimeElapsed(clock_t priorEvent)
{
     return ( std::clock() - priorEvent ) / (double)CLOCKS_PER_SEC;
}
double timeSince(clock_t priorEvent)
{
     return ( std::clock() - priorEvent ) / (double)CLOCKS_PER_SEC;
}

// DONE




class TreeNodeAndTime {
public:
    TreeNodeAndTime(KDTree::KDTreeNode<unsigned int> * tree, double time) {
        myTree = tree;
        myTime = time;
    }
    KDTree::KDTreeNode <unsigned int> * myTree;
    double myTime;
};






// the final parameter is modified; it will hold Detections associated with the 
// tracklet t.
void getAllDetectionsForTracklet(const std::vector<MopsDetection> & allDetections,
                              const Tracklet &t,
                              std::vector<MopsDetection> &detectionsForTracklet) 
{
    double start = std::clock();
    
    detectionsForTracklet.clear();
    std::set<unsigned int>::const_iterator trackletIndexIter;

    for (trackletIndexIter = t.indices.begin();
         trackletIndexIter != t.indices.end();
         trackletIndexIter++) {
        detectionsForTracklet.push_back(allDetections.at(*trackletIndexIter));
    }

    getAllDetectionsForTrackletTime += getTimeElapsed(start);
}






// given a tracklet, return the *temporally* earliest detection of this tracklet
// (the one with minimum MJD). 
Detection getFirstDetectionForTracklet(const std::vector<MopsDetection> &allDetections,
                                       const Tracklet &t) 
{
    double start = std::clock();
    if (t.indices.size() < 1) {
        LSST_EXCEPT(collapseTracklets::exceptions::BadParameterException,
                        "linkTracklets::getFirstDetectionForTracklet called with empty tracklet.");
    }

    MopsDetection toRet;
    bool foundOne = false;
    std::set<unsigned int>::const_iterator indexIter;
    for (indexIter = t.indices.begin(); indexIter != t.indices.end(); indexIter++) {
        Detection curDet = allDetections.at(*indexIter);
        if ((!foundOne) || 
            (toRet.getEpochMJD() > curDet.getEpochMJD())) {
            toRet = curDet;
            foundOne = true;
        }
    }

    getFirstDetectionForTrackletTime += getTimeElapsed(start);
    return toRet;

}










void makeTrackletTimeToTreeMap(const std::vector<MopsDetection> &allDetections,
                               const std::vector<Tracklet> &queryTracklets,
                               std::map<double, KDTree::KDTree <unsigned int> > &newMap)
{
    newMap.clear();
    //sort all tracklets by their first image time; make PointAndValues from
    //these so we can build a tree.
    std::map<double, std::vector<KDTree::PointAndValue<unsigned int> > > allTrackletPAVsMap;
    allTrackletPAVsMap.clear();
    for (unsigned int i = 0; i < queryTracklets.size(); i++) {
        MopsDetection firstDetection =  getFirstDetectionForTracklet(allDetections, queryTracklets.at(i));
        double firstDetectionTime = firstDetection.getEpochMJD();
        KDTree::PointAndValue<unsigned int> trackletPAV;
        std::vector<double> trackletPoint;

        trackletPoint.push_back(firstDetection.getRA());
        trackletPoint.push_back(firstDetection.getDec());
        trackletPoint.push_back(queryTracklets.at(i).velocityRA);
        trackletPoint.push_back(queryTracklets.at(i).velocityDec);
        trackletPAV.setPoint(trackletPoint);        

        trackletPAV.setValue(i);

        allTrackletPAVsMap[firstDetectionTime].push_back(trackletPAV);
    }

    // iterate over each time/pointAndValueVec pair and build a
    // corresponding time/KDTree pair.
    std::map<double, std::vector<KDTree::PointAndValue<unsigned int> > >::iterator PAVIter;
    for (PAVIter = allTrackletPAVsMap.begin(); PAVIter != allTrackletPAVsMap.end(); PAVIter++) {
        KDTree::KDTree<unsigned int> curTree(PAVIter->second, 4, LEAF_SIZE);
        newMap[PAVIter->first] = curTree;
    }

}



void addErrorRecurse(KDTree::KDTreeNode<unsigned int> * cur, linkTrackletsConfig searchConfig) 
{
    cur->getUBounds()->at(POINT_RA) += searchConfig.detectionLocationErrorThresh;
    cur->getLBounds()->at(POINT_RA) -= searchConfig.detectionLocationErrorThresh;
    cur->getUBounds()->at(POINT_RA_VELOCITY) += searchConfig.velocityErrorThresh;
    cur->getLBounds()->at(POINT_RA_VELOCITY) -= searchConfig.velocityErrorThresh;

    cur->getUBounds()->at(POINT_DEC) += searchConfig.detectionLocationErrorThresh;
    cur->getLBounds()->at(POINT_DEC) -= searchConfig.detectionLocationErrorThresh;
    cur->getUBounds()->at(POINT_DEC_VELOCITY) += searchConfig.velocityErrorThresh;
    cur->getLBounds()->at(POINT_DEC_VELOCITY) -= searchConfig.velocityErrorThresh;
    
    if (cur->hasLeftChild()) {
        addErrorRecurse(cur->getLeftChild(), searchConfig);        
    }
    if (cur->hasRightChild()) {
        addErrorRecurse(cur->getRightChild(), searchConfig);
    }
    
}


/*
 * we can remove a lot of math if we stretch the bounds of each tree node to account
 * for expected location/velocity error, rather than adding it in at every
 * visit.
 */
void addErrorToToTreeNodes(std::map<double, KDTree::KDTree<unsigned int> > &treeMap, 
                           linkTrackletsConfig searchConfig) 
{
    std::map<double, KDTree::KDTree<unsigned int> >::iterator treeIter;
    
    for (treeIter = treeMap.begin(); treeIter != treeMap.end(); treeIter++) {
        KDTree::KDTreeNode<unsigned int> *rootNode = treeIter->second.getRootNode();
        addErrorRecurse(rootNode, searchConfig);
    }
    
}






/*
 * update position and velocity given acceleration over time.
 */
inline void modifyWithAcceleration(double &position, double &velocity, 
                            double acceleration, double time)
{
    double start = std::clock();
    // use good ol' displacement = vt + .5a(t^2) 
    double newPosition = position + velocity*time + .5*acceleration*(time*time);
    double newVelocity = velocity + acceleration*time;
    position = newPosition;
    velocity = newVelocity;
    modifyWithAccelerationTime += timeSince(start);
}






/*
 * return true iff the range of positions and velocities described by
 * firstPositionMin/max and firstVelocityMin/Max will overlap the second
 * position/velocity ranges after factoring in acceleration and time.  note that
 * acceleration is assumed to work as either acceleration and deceleration.
 *
 * that is, we will decrease minVelocity by -1*acceleration and increase
 * maxVelocity by acceleration.  Analogously for position.
 *
 * we also "pad" out the max/min position and velocities using searchConfig.
 */
inline bool positionAndVelocityRangesOverlapAfterAcceleration(double firstPositionMin, double firstPositionMax, 
                                                              double firstVelocityMin, double firstVelocityMax,
                                                              double secondPositionMin, double secondPositionMax,
                                                              double secondVelocityMin, double secondVelocityMax,
                                                              double acceleration, double deltaTime)
{

    double start = std::clock();
    if (acceleration < 0) {
        LSST_EXCEPT(ctExcept::BadParameterException,
                   "positionAndVelocityRangesOverlapAfterAcceleration given negative acceleration; this doesn't make sense");
    }
    modifyWithAcceleration(firstPositionMax, firstVelocityMax, 
                           acceleration, deltaTime);

    modifyWithAcceleration(firstPositionMin, firstVelocityMin, 
                           -1.0 * acceleration, deltaTime);


    if ((KDTree::Common::regionsOverlap1D_unsafe(firstPositionMin, firstPositionMax,
                                                 secondPositionMin, secondPositionMax)) 
        && 
        (KDTree::Common::regionsOverlap1D_unsafe(firstVelocityMin, firstVelocityMax,
                                                 secondVelocityMin, secondVelocityMax))) {

        // the second region is "reachable" from the first region
        positionAndVelocityRangesOverlapAfterAccelerationTime += timeSince(start);
        return true;
    }
    else {
        positionAndVelocityRangesOverlapAfterAccelerationTime += timeSince(start);
        return false;
    }
}







/*
  return whether two KDTreeNodes holding

  [RA, Dec, RAvelocity, Decvelocity] -> tracklet ID

  are "compatible": that is, whether an object contained in the first node could
  reach the second node.  This is computed using the upper and lower bounds
  on the nodes' [RA, Dec, RAvelocity, Decvelocity] values.

  Note that we treat RA, Dec as unrelated and euclidean.

  all tracklets held in the node are assumed to start at the specified times.

  are compatible according to the rules parameters provided by searchConfig.

 */


//TBD: searches should really be extended with quadraticErrorThresh
bool areCompatible(TreeNodeAndTime  &nodeA,
                   TreeNodeAndTime  &nodeB,
                   linkTrackletsConfig searchConfig)
{
    double start = std::clock();
    KDTree::KDTreeNode<unsigned int> * first;
    double firstTime;
    KDTree::KDTreeNode<unsigned int> * second;
    double secondTime;

    // figure out which node occurs first

    if (nodeA.myTime > nodeB.myTime) {
        first = nodeB.myTree;
        firstTime = nodeB.myTime;

        second = nodeA.myTree;
        secondTime = nodeA.myTime;
    }
    else {
        first = nodeA.myTree;
        firstTime = nodeA.myTime;

        second = nodeB.myTree;
        secondTime = nodeB.myTime;
    }
    
    /*
     * we essentially treat RA and Dec as independent.
     *
     * Note that each node has a min, max RA position, dec position, RA
     * velocity, Dec velocity.
     *
     * for axis in [RA, Dec]:
     *
     *    extend the range (min/max) of node A's axis.velocity according to the
     *    maximum aceleration/deceleration provided by searchConfig and the time
     *    between node A and node B.
     *    
     *    extend the range (min/max) of node A's axis.position according to the
     *    acceleration, initial velocity, and time.
     *      
     *    if node A's position and velocity ranges intersect with node B's
     *    position and velocity ranges, then the two nodes are compatible.
     *
     *
     * this is an additional catch, however: we *also* need to start by
     * extending the velocity and positional ranges according to our upper
     * bounds on "reasonable" observational error (TBD)
     */

    bool RACompatible = false;
    bool DecCompatible = false;

    double deltaTime = secondTime - firstTime;

    //get upper and lower bounds of node's RA position, velocity
    //modify the positional bounds using error thresholds provided by user
    /*
      TBD: we need a way to track the possible extensions to the velocity range which would
      be caused by observational error.
    */


    double firstRAPositionMax = first->getUBounds()->at(POINT_RA) + searchConfig.detectionLocationErrorThresh;
    double firstRAPositionMin = first->getLBounds()->at(POINT_RA) - searchConfig.detectionLocationErrorThresh;
    double firstRAVelocityMax = first->getUBounds()->at(POINT_RA_VELOCITY) + searchConfig.velocityErrorThresh;
    double firstRAVelocityMin = first->getLBounds()->at(POINT_RA_VELOCITY) - searchConfig.velocityErrorThresh;


    double secondRAPositionMax = second->getUBounds()->at(POINT_RA) + searchConfig.detectionLocationErrorThresh;
    double secondRAPositionMin = second->getLBounds()->at(POINT_RA) - searchConfig.detectionLocationErrorThresh;
    double secondRAVelocityMax = second->getUBounds()->at(POINT_RA_VELOCITY) + searchConfig.velocityErrorThresh;
    double secondRAVelocityMin = second->getLBounds()->at(POINT_RA_VELOCITY) - searchConfig.velocityErrorThresh;

    //extend first RA position, velocity bounds according to acceleration
    RACompatible = positionAndVelocityRangesOverlapAfterAcceleration(firstRAPositionMin, firstRAPositionMax,
                                                                     firstRAVelocityMin, firstRAVelocityMax,
                                                                     secondRAPositionMin, secondRAPositionMax,
                                                                     secondRAVelocityMin, secondRAVelocityMax,
                                                                     searchConfig.maxRAAccel, deltaTime);
    if (RACompatible == false) {
        areCompatibleTime += timeSince(start);
        return false;
    }

    //get upper and lower bounds of node's Dec position, velocity
    
    double firstDecPositionMax = first->getUBounds()->at(POINT_DEC) + searchConfig.detectionLocationErrorThresh;
    double firstDecPositionMin = first->getLBounds()->at(POINT_DEC) - searchConfig.detectionLocationErrorThresh;
    double firstDecVelocityMax = first->getUBounds()->at(POINT_DEC_VELOCITY) + searchConfig.velocityErrorThresh;
    double firstDecVelocityMin = first->getLBounds()->at(POINT_DEC_VELOCITY) - searchConfig.velocityErrorThresh;

    double secondDecPositionMax = second->getUBounds()->at(POINT_DEC) + searchConfig.detectionLocationErrorThresh;
    double secondDecPositionMin = second->getLBounds()->at(POINT_DEC) - searchConfig.detectionLocationErrorThresh;
    double secondDecVelocityMax = second->getUBounds()->at(POINT_DEC_VELOCITY) + searchConfig.velocityErrorThresh;
    double secondDecVelocityMin = second->getLBounds()->at(POINT_DEC_VELOCITY) - searchConfig.velocityErrorThresh;

    //extend first Dec position, velocity bounds according to acceleration
    
    //extend first Dec position, velocity bounds according to acceleration
    DecCompatible = positionAndVelocityRangesOverlapAfterAcceleration(firstDecPositionMin, firstDecPositionMax,
                                                                      firstDecVelocityMin, firstDecVelocityMax,
                                                                      secondDecPositionMin, secondDecPositionMax,
                                                                      secondDecVelocityMin, secondDecVelocityMax,
                                                                      searchConfig.maxDecAccel, deltaTime);

    if (DecCompatible == false) {
        areCompatibleTime += timeSince(start);
        return false;
    }
    
    areCompatibleTime += timeSince(start);
    return true;
}









/*
  this is a potentially dangerous way of doing things.  we really need to know
  the min and max possible velocity for a tracklet, not the 'best fit' velocity
  for the tracklet.  (we can implicitly calculate the min and max if we assume
  2-point tracklets; but with many-point tracklets some additional possible
  error creeps in).

  TBD: be a tad more careful. for 2-point tracklets this is easy, for longer
  tracklets it may be trickier.
 */
void setTrackletVelocities(const std::vector<MopsDetection> &allDetections,
                           std::vector<Tracklet> &queryTracklets)
{
    for (unsigned int i = 0; i < queryTracklets.size(); i++) {
        Tracklet *curTracklet = &queryTracklets.at(i);
        std::vector <MopsDetection> trackletDets;
        getAllDetectionsForTracklet(allDetections, *curTracklet, trackletDets);

        std::vector<double> RASlopeAndOffset;
        std::vector<double> DecSlopeAndOffset;
        rmsLineFit::leastSquaresSolveForRADecLinear(&trackletDets,
                                                    RASlopeAndOffset,
                                                    DecSlopeAndOffset);
        
        curTracklet->velocityRA = RASlopeAndOffset.at(0);
        curTracklet->velocityDec = DecSlopeAndOffset.at(0);
    }

}






void getBestFitVelocityAndAcceleration(std::vector<double> positions, std::vector<double>times,
                                       double & velocity, double &acceleration, double &position0)
{
    double start = std::clock();
    if (positions.size() != times.size()) {
        LSST_EXCEPT(ctExcept::ProgrammerErrorException,
                    "getBestFitVelocityAndAcceleration: position and time vectors not same size!");
    }

    /* we're using GSL for this. this is roughly adapted from the GSL
     * documentation; see
     * http://www.gnu.org/software/gsl/manual/html_node/Fitting-Examples.html
     */ 

    gsl_vector * y = gsl_vector_alloc(positions.size());
    gsl_matrix * X = gsl_matrix_alloc(positions.size(), 3);
        
    for (unsigned int i = 0; i < positions.size(); i++) {
        gsl_matrix_set(X, i, 0, 1.0);
        gsl_matrix_set(X, i, 1, times.at(i) );
        gsl_matrix_set(X, i, 2, times.at(i)*times.at(i) );
        gsl_vector_set(y, i, positions.at(i));
    }
    gsl_vector * c = gsl_vector_alloc(3); // times*c = positions 
    gsl_multifit_linear_workspace * work = gsl_multifit_linear_alloc (positions.size(), 3);
    gsl_matrix * covariance = gsl_matrix_alloc(3,3);
    double chiSquared = 0;    
    // TBD: check return values for error, etc
    gsl_multifit_linear(X, y, c, covariance, &chiSquared, work);
    position0    = gsl_vector_get(c,0);
    velocity     = gsl_vector_get(c,1);
    acceleration = gsl_vector_get(c,2);

    gsl_vector_free(y);
    gsl_matrix_free(X);
    gsl_vector_free(c);
    gsl_matrix_free(covariance);
    gsl_multifit_linear_free(work);
    getBestFitVelocityAndAccelerationTime += timeSince(start);
}








void getBestFitVelocityAndAccelerationForTracklets(const std::vector<MopsDetection> &allDetections,
                                                   const std::vector<Tracklet> &queryTracklets,
                                                   const unsigned int trackletID1,
                                                   const unsigned int trackletID2,
                                                   double & RAVelocity, double & RAAcceleration, 
                                                   double & RAPosition0,
                                                   double & DecVelocity, double & DecAcceleration, 
                                                   double & DecPosition0, 
                                                   double & time0) 
{
    double start = std::clock();
    //get the detection IDs from tracklet 1 and tracklet 2 into a std::set.

    std::set <unsigned int> allDetectionIDs;
    std::set<unsigned int>::const_iterator trackletDetIter;
    for (trackletDetIter = queryTracklets.at(trackletID1).indices.begin(); 
         trackletDetIter != queryTracklets.at(trackletID1).indices.end();
         trackletDetIter++) {
        allDetectionIDs.insert(*trackletDetIter);
    }
    for (trackletDetIter = queryTracklets.at(trackletID2).indices.begin(); 
         trackletDetIter != queryTracklets.at(trackletID2).indices.end();
         trackletDetIter++) {
        allDetectionIDs.insert(*trackletDetIter);
    }

    // use a helper function to get the velocities and accelerations in RA, Dec.
    std::vector<double> RAs;
    std::vector<double> Decs;
    std::vector<double> times;
    std::set<unsigned int>::const_iterator detIter;

    double firstTime = allDetections.at(*allDetectionIDs.begin()).getEpochMJD();
    time0 = firstTime;
    
    for (detIter = allDetectionIDs.begin(); detIter != allDetectionIDs.end(); detIter++) {
        const Detection * curDetection = &(allDetections.at(*detIter));
        RAs.push_back(curDetection->getRA());
        Decs.push_back(curDetection->getDec());
        times.push_back(curDetection->getEpochMJD() - firstTime);
    }
    getBestFitVelocityAndAcceleration(RAs, times,  RAVelocity,  RAAcceleration,  RAPosition0 );
    getBestFitVelocityAndAcceleration(Decs, times, DecVelocity, DecAcceleration, DecPosition0);
    getBestFitVelocityAndAccelerationForTrackletsTime += timeSince(start);
}




/* 
 * addBestCompatibleTrackletsAndDetectionsToTrack: this function uses an RA/Dec
 * motion prediction and finds the most compatible points owned by tracklets in
 * the candidateTrackletsIDs vector.  The best-fit points are added in order of
 * fit quality, with at most one detection added per image time.  "Compatible"
 * here is defined by the parameters in searchConfig.
 *
 * Detection IDs and the IDs of the detections' parents are added to newTrack's
 * relevant fields.
 */
void addBestCompatibleTrackletsAndDetectionsToTrack(const std::vector<MopsDetection> &allDetections, 
                                                    const std::vector<Tracklet> &allTracklets, 
                                                    const std::vector<unsigned int> candidateTrackletIDs, 
                                                    double RAVelocity, double RAAcceleration, double RAPosition0,
                                                    double DecVelocity, double DecAcceleration, double DecPosition0,
                                                    double time0,
                                                    const linkTrackletsConfig &searchConfig,
                                                    Track &newTrack) 
{
    double start = std::clock();
    std::map<double, std::pair<unsigned int, unsigned int> > scoreToIDsMap;
    /* the scoreToIDsMap will hold scores as the key, and use a pair of (detectionID, parent tracklet ID)
       to identify the detection with that score.
     */
    std::vector<unsigned int>::const_iterator trackletIDIter;
    std::set<unsigned int>::const_iterator detectionIDIter;
    for (trackletIDIter = candidateTrackletIDs.begin();
         trackletIDIter != candidateTrackletIDs.end();
         trackletIDIter++) {
        const Tracklet * curTracklet = &allTracklets.at(*trackletIDIter);
        for (detectionIDIter =  curTracklet->indices.begin();
             detectionIDIter != curTracklet->indices.end();
             detectionIDIter++) {
            double detMJD = allDetections.at(*detectionIDIter).getEpochMJD();
            double detRA  = allDetections.at(*detectionIDIter).getRA();
            double detDec = allDetections.at(*detectionIDIter).getDec();
            double timeOffset = detMJD - time0;
            double predRA  = RAPosition0 + RAVelocity*timeOffset + RAAcceleration*timeOffset*timeOffset;
            double predDec = DecPosition0 + DecVelocity*timeOffset + DecAcceleration*timeOffset*timeOffset;
            double distance = KDTree::Common::angularDistanceRADec_deg(detRA, detDec, predRA, predDec);

            if (distance < searchConfig.quadraticFitErrorThresh + searchConfig.detectionLocationErrorThresh)  {
                scoreToIDsMap[distance] = std::make_pair(*detectionIDIter, *trackletIDIter);
            }            
        }
    }
    
    /* initialize a list of image times present in the track already. */
    std::set<double> trackMJDs;
    std::set<unsigned int>::const_iterator trackDetectionIndices;
    for (trackDetectionIndices =  newTrack.componentDetectionIndices.begin();
         trackDetectionIndices != newTrack.componentDetectionIndices.end();
         trackDetectionIndices++) {
        trackMJDs.insert(allDetections.at(*trackDetectionIndices).getEpochMJD());        
    }
    
    /* add detections (and their parent tracklets) in order of 'score' (distance
     * from best-fit line) without adding any detections from
     * already-represented image times
     */
    std::map<double, std::pair<unsigned int, unsigned int> >::iterator mapIter;

    for (mapIter = scoreToIDsMap.begin(); mapIter != scoreToIDsMap.end(); mapIter++) {
        const Detection * associatedDetection = &allDetections.at(mapIter->second.first);

        // the next line is wacky C++ talk for "if associatedDetection's MJD not in trackMJDs"

        if (trackMJDs.find(associatedDetection->getEpochMJD()) == trackMJDs.end()) {
            /* add this detection and tracklet to the track and add the associated MJD to 
             * trackMJDs */
            
            trackMJDs.insert(associatedDetection->getEpochMJD());
            newTrack.componentDetectionIndices.insert(mapIter->second.first);
            newTrack.componentTrackletIndices.insert(mapIter->second.second);
        }
    }
    addBestCompatibleTrackletsAndDetectionsToTrackTime += timeSince(start);
}







bool trackMeetsRequirements(const std::vector<MopsDetection> & allDetections, 
                            const Track &newTrack, 
                            double RAVelocity, double RAAcceleration, double RAPosition0,
                            double DecVelocity, double DecAcceleration, double DecPosition0,
                            double time0,
                            linkTrackletsConfig searchConfig)
{
    double start = std::clock();
    bool meetsReqs = true;

    if (newTrack.componentTrackletIndices.size() < searchConfig.minSupportTracklets + 2) {
        meetsReqs = false;
    }

    if (newTrack.componentDetectionIndices.size() < searchConfig.minDetectionsPerTrack) {
        meetsReqs = false;
    }

    trackMeetsRequirementsTime += timeSince(start);
    return meetsReqs;
    
    
}







/*
 * checks that endpoint tracklets are compatible.  
 * 
 * this checks several things and returns true iff all are true:
 *
 * - all points are within error threshholds of the predicted location (as predicected by *Velocity, *Acceleration *Position0).
 * - the first and last detection are at least minTimeSeparation apart 
 * - the best-fit accelerations are within min/max bounds
 * 
 */
bool endpointTrackletsAreCompatible(const std::vector<MopsDetection> & allDetections, 
                                    const std::vector<Tracklet> &allTracklets,
                                    unsigned int trackletID1,
                                    unsigned int trackletID2,
                                    double RAVelocity, double RAAcceleration, double RAPosition0,
                                    double DecVelocity, double DecAcceleration, double DecPosition0,
                                    double time0,
                                    linkTrackletsConfig searchConfig)
{
    double start = std::clock();
    bool allOK = true;

    if (RAAcceleration > searchConfig.maxRAAccel) {
        allOK = false;
    }
    if (DecAcceleration > searchConfig.maxDecAccel) {
        allOK = false;
    }

    if (allOK == true) {
        
        std::set<unsigned int> allDetectionIndices; // the set of all detections held by both tracklets
        std::set<unsigned int>::const_iterator detIter;
        for (detIter = allTracklets.at(trackletID1).indices.begin();
             detIter != allTracklets.at(trackletID1).indices.end();
             detIter++) {
            allDetectionIndices.insert(*detIter);
        }
        for (detIter = allTracklets.at(trackletID2).indices.begin();
             detIter != allTracklets.at(trackletID2).indices.end();
             detIter++) {
            allDetectionIndices.insert(*detIter);
        }
        
        
        for (detIter = allDetectionIndices.begin();
             detIter != allDetectionIndices.end();
             detIter++) {
            double detMJD = allDetections.at(*detIter).getEpochMJD();
            double timeOffset = detMJD - time0;
            double RAPred = RAPosition0 + RAVelocity*timeOffset + RAAcceleration*timeOffset*timeOffset;
            double DecPred = DecPosition0 + DecVelocity*timeOffset + DecAcceleration*timeOffset*timeOffset;
            double observedRA = allDetections.at(*detIter).getRA();
            double observedDec = allDetections.at(*detIter).getDec();
            double distanceError = 
                KDTree::Common::angularDistanceRADec_deg(RAPred, DecPred, observedRA, observedDec);
            if (distanceError > searchConfig.quadraticFitErrorThresh + searchConfig.detectionLocationErrorThresh) {
                allOK = false;
            }
        }
        
        
        if (allOK == true) {
            //check that time separation is good
            double minMJD, maxMJD;
            detIter = allDetectionIndices.begin();
            minMJD = allDetections.at(*detIter).getEpochMJD();
            maxMJD = minMJD;
            for (detIter = allDetectionIndices.begin();
                 detIter != allDetectionIndices.end();
                 detIter++) {
                double thisMJD = allDetections.at(*detIter).getEpochMJD();
                if (thisMJD < minMJD) { 
                    minMJD = thisMJD; }
                if (thisMJD > maxMJD) {
                    maxMJD = thisMJD;
                }
            }
            if (maxMJD - minMJD < searchConfig.minEndpointTimeSeparation) {
                allOK = false;
            }
        }
    }
    endpointTrackletsAreCompatibleTime += timeSince(start);
    return allOK;
}









/*
 * this is called when all endpoint nodes (i.e. model nodes) and support nodes
 * are leaves.  model nodes and support nodes are expected to be mutually compatible.
 */
void buildTracksAddToResults(const std::vector<MopsDetection> &allDetections,
                             const std::vector<Tracklet> &allTracklets,
                             linkTrackletsConfig searchConfig,
                             TreeNodeAndTime &firstEndpoint,
                             TreeNodeAndTime &secondEndpoint,
                             std::vector<TreeNodeAndTime> &supportNodes,
                             std::vector<Track> & results)
{
    double start = std::clock();
    buildTracksAddToResultsVisits++;

    if ((firstEndpoint.myTree->isLeaf() == false) ||
        (secondEndpoint.myTree->isLeaf() == false)) {
        LSST_EXCEPT(ctExcept::ProgrammerErrorException, 
                    "buildTracksAddToResults got non-leaf nodes, must be a bug!");
    }
    for (unsigned int i = 0; i < supportNodes.size(); i++) {
        if (supportNodes.at(i).myTree->isLeaf() == false) {
            LSST_EXCEPT(ctExcept::ProgrammerErrorException, 
                        "buildTracksAddToResults got non-leaf nodes, must be a bug!");            
        }
    }

    std::vector<KDTree::PointAndValue<unsigned int> >::const_iterator firstEndpointIter;
    std::vector<KDTree::PointAndValue<unsigned int> >::const_iterator secondEndpointIter;
    std::vector<TreeNodeAndTime>::const_iterator supportNodeIter;
    std::vector<KDTree::PointAndValue <unsigned int> >::const_iterator supportPointIter;
    for (firstEndpointIter = firstEndpoint.myTree->getMyData()->begin();
         firstEndpointIter != firstEndpoint.myTree->getMyData()->end();
         firstEndpointIter++) {

        for (secondEndpointIter = secondEndpoint.myTree->getMyData()->begin();
             secondEndpointIter != secondEndpoint.myTree->getMyData()->end();
             secondEndpointIter++) {


            /* figure out the rough quadratic track fitting the two endpoints.
             * if error is too large, quit. Otherwise, choose support points
             * from the support nodes, using best-fit first, and ignoring those
             * too far off the line.  If we get enough points, return a track.
            */

            double RAVelocity, DecVelocity, RAAcceleration, DecAcceleration;
            double RAPosition0, DecPosition0;
            double time0;
            getBestFitVelocityAndAccelerationForTracklets(allDetections,
                                                          allTracklets, 
                                                          firstEndpointIter->getValue(),
                                                          secondEndpointIter->getValue(),
                                                          RAVelocity,RAAcceleration,RAPosition0,
                                                          DecVelocity,DecAcceleration,DecPosition0,
                                                          time0);

            if (endpointTrackletsAreCompatible(allDetections, 
                                               allTracklets,
                                               firstEndpointIter->getValue(),
                                               secondEndpointIter->getValue(),
                                               RAVelocity,RAAcceleration,RAPosition0,
                                               DecVelocity,DecAcceleration,DecPosition0,
                                               time0,searchConfig)) {

                compatibleEndpointsFound++;
                // create a new track with these endpoints
                Track newTrack;
                std::vector<unsigned int> candidateTrackletIDs;
                candidateTrackletIDs.push_back(firstEndpointIter->getValue() );
                candidateTrackletIDs.push_back(secondEndpointIter->getValue());
                addBestCompatibleTrackletsAndDetectionsToTrack(allDetections, allTracklets, candidateTrackletIDs, 
                                                               RAVelocity, RAAcceleration, RAPosition0,
                                                               DecVelocity, DecAcceleration, DecPosition0,
                                                               time0,searchConfig,
                                                               newTrack);
                
                candidateTrackletIDs.clear();
                
                /* add the best compatible support points */

                // put all support tracklet IDs in curSupportNodeData, then call
                // addBestCompatibleTrackletsAndDetectionsToTrack
                for (supportNodeIter = supportNodes.begin(); supportNodeIter != supportNodes.end();
                     supportNodeIter++) {
                    const std::vector<KDTree::PointAndValue <unsigned int> > * curSupportNodeData;
                    curSupportNodeData = supportNodeIter->myTree->getMyData(); 
                    for (supportPointIter  = curSupportNodeData->begin(); 
                         supportPointIter != curSupportNodeData->end();
                         supportPointIter++) {
                        candidateTrackletIDs.push_back(supportPointIter->getValue());
                    }
                }
                
                addBestCompatibleTrackletsAndDetectionsToTrack(allDetections, allTracklets, candidateTrackletIDs,
                                                               RAVelocity, RAAcceleration, RAPosition0,
                                                               DecVelocity, DecAcceleration, DecPosition0, time0,
                                                               searchConfig,
                                                               newTrack);
                
                if (trackMeetsRequirements(allDetections, newTrack,  
                                           RAVelocity, RAAcceleration, RAPosition0, 
                                           DecVelocity, DecAcceleration, DecPosition0,
                                           time0,searchConfig)) {
                    results.push_back(newTrack);
                }
            }
        }    
    }
    buildTracksAddToResultsTime += timeSince(start);
}






bool areAllLeaves(const std::vector<TreeNodeAndTime> &nodeArray) {
    double start = std::clock();
    bool allLeaves = true;
    std::vector<TreeNodeAndTime>::const_iterator treeIter;
    unsigned int count = 0;
    for (treeIter = nodeArray.begin(); 
         (treeIter != nodeArray.end() && (allLeaves == true));
         treeIter++) {
        if (treeIter->myTree->isLeaf() == false) {
            allLeaves = false;
        }
        count++;
    }
    areAllLeavesTime += timeSince(start);
    return allLeaves;
}







double nodeWidth(KDTree::KDTreeNode<unsigned int> *node)
{
    double start = std::clock();
    double width = 1;
    for (unsigned int i = 0; i < 4; i++) {
        width *= node->getUBounds()->at(i) - node->getLBounds()->at(i);
    }
    nodeWidthTime += timeSince(start);
    return width;    
}



/*
 * this is, roughly, the algorithm presented in http://arxiv.org/abs/astro-ph/0703475v1:
 * 
 * Efficient intra- and inter-night linking of asteroid detections using kd-trees
 * 
 * The "proper" version of the algorithm (as presented in psuedocode om the
 * document) is a little different; basically, he has us recurring only on
 * endpoints, and splitting support nodes repeatedly at each call, until they
 * are no longer "too wide".  Unfortunately, I found that neither my intuition
 * nor Kubica's implementation made clear the definition of "too wide".
 *
 * this implementation is more like the one Kubica did in his code. at every
 * step, we check all support nodes for compatibility, splitting each one. we
 * then split one model node and recurse. 
 */
void doLinkingRecurse2(const std::vector<MopsDetection> &allDetections,
                       const std::vector<Tracklet> &allTracklets,
                       linkTrackletsConfig searchConfig,
                       TreeNodeAndTime &firstEndpoint,
                       TreeNodeAndTime &secondEndpoint,
                       std::vector<TreeNodeAndTime> &supportNodes,
                       std::vector<Track> & results,
                       int iterationsTillSplit)
{
    double start = std::clock();
    doLinkingRecurseVisits++;
 
    if (areCompatible(firstEndpoint, secondEndpoint, searchConfig) == false)
    {
        // poor choice of model nodes (endpoint nodes)! give up.
        doLinkingRecurseTime += timeSince(start);
        return;
    }
    else 
    {

        std::set<double> uniqueSupportMJDs;
        std::vector<TreeNodeAndTime> newSupportNodes;
        std::vector<TreeNodeAndTime>::iterator supportNodeIter;
        
        /* look through untested support nodes, find the ones that are compatible
         * with the model nodes, add their children to newSupportNodes */

        for (supportNodeIter  = supportNodes.begin();
             supportNodeIter != supportNodes.end();
             supportNodeIter++) {

            if (iterationsTillSplit <= 0) {
                bool firstEndpointCompatible;
                bool secondEndpointCompatible;

                firstEndpointCompatible =  areCompatible(firstEndpoint, *supportNodeIter, searchConfig);
                secondEndpointCompatible = areCompatible(secondEndpoint, *supportNodeIter, searchConfig);
                
                if (firstEndpointCompatible && secondEndpointCompatible)
                {   
                    if (supportNodeIter->myTree->isLeaf()) {
                        newSupportNodes.push_back(*supportNodeIter);
                        uniqueSupportMJDs.insert(supportNodeIter->myTime);
                    }
                    else {
                        if (supportNodeIter->myTree->hasLeftChild()) {
                            newSupportNodes.push_back(TreeNodeAndTime(supportNodeIter->myTree->getLeftChild(), supportNodeIter->myTime));
                            uniqueSupportMJDs.insert(supportNodeIter->myTime);
                        }
                        if (supportNodeIter->myTree->hasRightChild()) {
                            newSupportNodes.push_back(TreeNodeAndTime(supportNodeIter->myTree->getRightChild(), supportNodeIter->myTime));
                            uniqueSupportMJDs.insert(supportNodeIter->myTime);
                        }
                    }
                }
            }
            else {
                // iterationsTillSplit is non-zero; 
                // don't do any real work; just copy the previous support nodes. we'll do this
                // momentarily; but go ahead and add their support MJDs now.
                uniqueSupportMJDs.insert(supportNodeIter->myTime);                
            }
        }
        

        if (iterationsTillSplit <= 0) {
            iterationsTillSplit = ITERATIONS_PER_SPLIT;
        }
        else{
            // we still need to get newSupportNodes set up. just use the old ones.
            newSupportNodes = supportNodes;
        }

        if (uniqueSupportMJDs.size() < searchConfig.minSupportTracklets) {
            // we can't possibly have enough distinct support tracklets between endpoints
            doLinkingRecurseTime += timeSince(start);
            return; 
        }
        else 
        {
            // we have enough model nodes, and enough support nodes.
            // if they are all leaves, then start building tracks.
            // if they are not leaves, split one of them and recurse.

            if (firstEndpoint.myTree->isLeaf() && secondEndpoint.myTree->isLeaf() &&
                areAllLeaves(newSupportNodes)) {
                
                // TBD: actually, we need to filter newSupportNodes one more time here, since 
                // we checked for compatibility, then split off the children. of course, buildTracksAddToResults won't 
                // be affected with regards to correctness, just performance. So it may not even be wise to add a
                // mostly-needless check.
                buildTracksAddToResults(allDetections, allTracklets, searchConfig,
                                        firstEndpoint, secondEndpoint, newSupportNodes,
                                        results);
            }
            else  {
               
                iterationsTillSplit -= 1;

                // find the "widest" node, where width is just the product
                // of RA range, Dec range, RA velocity range, Dec velocity range.
                // we will split that node and recurse.
                
                double firstEndpointWidth = nodeWidth(firstEndpoint.myTree);
                double secondEndpointWidth = nodeWidth(secondEndpoint.myTree);
                
                // don't consider splitting endpoint nodes which are actually leaves! give them negative width to hack selection process.
                if (firstEndpoint.myTree->isLeaf()) {
                    firstEndpointWidth = -1;
                }
                if (secondEndpoint.myTree->isLeaf()) {
                    secondEndpointWidth = -1;
                }

                // choose the widest model node, split it and recurse!                                
                if ( (KDTree::Common::areEqual(firstEndpointWidth, -1)) &&
                     (KDTree::Common::areEqual(secondEndpointWidth, -1)) ) {
                    // in this case, our endpoints are leaves, but not all our support nodes are.
                    // just call this function again until they *are* all leaves.
                    iterationsTillSplit = 0;
                    doLinkingRecurseTime += timeSince(start);
                    doLinkingRecurse2(allDetections, allTracklets, searchConfig,
                                      firstEndpoint, secondEndpoint,
                                      newSupportNodes, results, iterationsTillSplit);
                }
                else if (firstEndpointWidth >= secondEndpointWidth) {

                    //"widest" node is first endpoint, recurse twice using its children
                    // in its place.  
                    
                    if ((! firstEndpoint.myTree->hasLeftChild()) && (!firstEndpoint.myTree->hasRightChild())) {
                        throw LSST_EXCEPT(ctExcept::ProgrammerErrorException, "Recursing in a leaf node (first endpoint), must be a bug!");
                    }

                    if (firstEndpoint.myTree->hasLeftChild())
                    {
                        TreeNodeAndTime newTAT(firstEndpoint.myTree->getLeftChild(), firstEndpoint.myTime);
                        doLinkingRecurseTime += timeSince(start);
                        doLinkingRecurse2(allDetections, allTracklets, searchConfig,
                                          newTAT,secondEndpoint,
                                          newSupportNodes,
                                          results, iterationsTillSplit); 
                    }
                    
                    if (firstEndpoint.myTree->hasRightChild())
                    {
                        TreeNodeAndTime newTAT(firstEndpoint.myTree->getRightChild(), firstEndpoint.myTime);
                        doLinkingRecurseTime += timeSince(start);
                        doLinkingRecurse2(allDetections, allTracklets, searchConfig,
                                          newTAT,secondEndpoint,
                                          newSupportNodes,
                                          results, iterationsTillSplit);  
                    }
                }
                else {
                    //"widest" node is second endpoint, recurse twice using its children
                    // in its place
                    
                    if ((!secondEndpoint.myTree->hasLeftChild()) && (!secondEndpoint.myTree->hasRightChild())) {
                        throw LSST_EXCEPT(ctExcept::ProgrammerErrorException, "Recursing in a leaf node (second endpoint), must be a bug!");
                    }

                    if (secondEndpoint.myTree->hasLeftChild())
                    {
                        TreeNodeAndTime newTAT(secondEndpoint.myTree->getLeftChild(), secondEndpoint.myTime);
                        doLinkingRecurseTime += timeSince(start);
                        doLinkingRecurse2(allDetections, allTracklets, searchConfig,
                                          firstEndpoint,newTAT,
                                          newSupportNodes,
                                          results, iterationsTillSplit);            
                    }
                    
                    if (secondEndpoint.myTree->hasRightChild())
                    {
                        TreeNodeAndTime newTAT(secondEndpoint.myTree->getRightChild(), secondEndpoint.myTime);
                        doLinkingRecurseTime += timeSince(start);
                        doLinkingRecurse2(allDetections, allTracklets, searchConfig,
                                          firstEndpoint,newTAT,
                                          newSupportNodes,
                                          results, iterationsTillSplit);
                        
                    }
                }
            }                        
        }
    }
}










void doLinking(const std::vector<MopsDetection> &allDetections,
               std::vector<Tracklet> &allTracklets,
               linkTrackletsConfig searchConfig,
               std::map<double, KDTree::KDTree <unsigned int> > &trackletTimeToTreeMap,
               std::vector<Track> &results)
{
    /* for every pair of trees, using the set of every intermediate (temporally) tree as a
     * set possible support nodes, call the recursive linker.
     */ 
    /*
     * implementation detail: note that there is a difference between a KDTree
     * and a KDTreeNode.  KDTrees are the "outer layer" and don't really hold
     * data; KDTreeNodes are where the data lives.
     * 
     * In other programs, KDTreeNodes are hidden from the user, but
     * linkTracklets will use them directly.
     *
     * In doLinking_recurse, we will only deal with KDTreeNodes. So in
     * doLinking, we start the searching with the head (i.e. root) node of each
     * tree.
     */
    bool DEBUG = true;

    bool limitedRun = true;
    double limitedRunFirstEndpoint = 49616.249649999998 ;
    double limitedRunSecondEndpoint = 49623.023787999999 ;

    doLinkingRecurseVisits = 0;
    buildTracksAddToResultsVisits = 0;
    compatibleEndpointsFound = 0;
    getAllDetectionsForTrackletTime = 0; 
    getFirstDetectionForTrackletTime = 0; 
    modifyWithAccelerationTime = 0; 
    positionAndVelocityRangesOverlapAfterAccelerationTime = 0; 
    areCompatibleTime = 0; 
    getBestFitVelocityAndAccelerationTime = 0;
    getBestFitVelocityAndAccelerationForTrackletsTime = 0; 
    addBestCompatibleTrackletsAndDetectionsToTrackTime = 0; 
    trackMeetsRequirementsTime = 0; 
    endpointTrackletsAreCompatibleTime = 0; 
    buildTracksAddToResultsTime = 0; 
    areAllLeavesTime = 0; 
    nodeWidthTime = 0; 
    addAllDetectedObjectsToSetTime = 0; 
    doLinkingRecurseTime = 0;

    if (DEBUG) {
        std:: cout << "all MJDs: ";
        std::map<double, KDTree::KDTree<unsigned int> >::const_iterator mapIter;
        for (mapIter = trackletTimeToTreeMap.begin();
             mapIter != trackletTimeToTreeMap.end();
             mapIter++) {
            std::cout << std::setprecision (10) << mapIter->first << " ";
        }
        std::cout << std::endl;
    }

    std::map<double, KDTree::KDTree<unsigned int> >::const_iterator firstEndpointIter;
    for (firstEndpointIter = trackletTimeToTreeMap.begin(); 
         firstEndpointIter != trackletTimeToTreeMap.end(); 
         firstEndpointIter++)
    {
        std::map<double, KDTree::KDTree<unsigned int> >::const_iterator secondEndpointIter;
        std::map<double, KDTree::KDTree<unsigned int> >::const_iterator afterFirstIter = firstEndpointIter;
        afterFirstIter++;

        
        if ((!limitedRun) || (KDTree::Common::areEqual(firstEndpointIter->first, limitedRunFirstEndpoint))) {
            
            
            for (secondEndpointIter = afterFirstIter; 
                 secondEndpointIter != trackletTimeToTreeMap.end(); 
                 secondEndpointIter++)
            {
                /* if there is sufficient time between the first and second nodes, then try
                   doing the recursive linking using intermediate times as support nodes.
                */
                
                if ((!limitedRun) || (KDTree::Common::areEqual(secondEndpointIter->first, limitedRunSecondEndpoint))) {
                    
                    
                    if (secondEndpointIter->first - firstEndpointIter->first >= searchConfig.minEndpointTimeSeparation) {                
                        
                        if (DEBUG) {
                            time_t rawtime;
                            struct tm * timeinfo;
                    
                            time ( &rawtime );
                            timeinfo = localtime ( &rawtime );                    

                            std::cout << "attempting linking between times " << std::setprecision(12)  
                                      << firstEndpointIter->first << " and " 
                                      << std::setprecision(12) << secondEndpointIter->first << std::endl;
                            std::cout << " current wall-clock time is " << asctime (timeinfo) << std::endl;
                    
                        }

                        // get all intermediate points as support nodes.
                
                        /* note that std::maps are sorted by their key, which in this case
                         * is time.  ergo between firstEndpointIter and secondEndpointIter
                         * is *EVERY* tree (and ergo every tracklet) which happened between
                         * the first endpoint's tracklets and the second endpoint's
                         * tracklets.
                         */
                
                        std::vector<TreeNodeAndTime > supportPoints;
                        std::map<double, KDTree::KDTree<unsigned int> >::const_iterator supportPointIter;
                        if (DEBUG) {
                            //std::cout << "intermediate times: " ;
                        }
                        for (supportPointIter = afterFirstIter;
                             supportPointIter != secondEndpointIter;
                             supportPointIter++) {
                    
                            /* don't pass along second tracklets which are 'too close' to the endpoints; see
                               linkTracklets.h for more comments */
                            if ((fabs(supportPointIter->first - firstEndpointIter->first) > 
                                 searchConfig.minSupportToEndpointTimeSeparation) 
                                && 
                                (fabs(supportPointIter->first - secondEndpointIter->first) > 
                                 searchConfig.minSupportToEndpointTimeSeparation)) {
                        
                                TreeNodeAndTime tmpTAT(supportPointIter->second.getRootNode(), supportPointIter->first);
                                supportPoints.push_back(tmpTAT);
                                if (DEBUG) {
                                    //std::cout << std::setprecision(10) << tmpTAT.myTime << " ";
                                }
                            }
                        }
                
                        if (DEBUG) {std::cout << "\n";}

                        TreeNodeAndTime firstEndpoint(firstEndpointIter->second.getRootNode(), 
                                                      firstEndpointIter->first);
                        TreeNodeAndTime secondEndpoint(secondEndpointIter->second.getRootNode(),
                                                       secondEndpointIter->first);
                
                        //call the recursive linker with the endpoint nodes and support point nodes.

                        double last;
                        if (limitedRun) {
                            last = std::clock();
                        }

                        doLinkingRecurse2(allDetections, allTracklets, searchConfig,
                                         firstEndpoint, secondEndpoint,
                                         supportPoints,  
                                          results, 2);

                        if (limitedRun)
                        {
                            double dif = getTimeElapsed(last);
                            std::cout << "doLinkingRecurse2 took " << std::fixed << std::setprecision(10)
                                      << dif << " seconds." << std::endl;
                        }


                    }
                }
            }
        }
    }
    std::cout << "TIMING STATS: \n---------------------\n";
    std::cout << "getAllDetectionsForTracklet: :\t" << getAllDetectionsForTrackletTime << "sec\n"; 
    std::cout << "getFirstDetectionForTracklet:\t" << getFirstDetectionForTrackletTime << "sec\n"; 
    std::cout << "modifyWithAcceleration:\t" << modifyWithAccelerationTime << "sec\n"; 
    std::cout << "positionAndVelocityRangesOverlapAfterAcceleration:\t" << positionAndVelocityRangesOverlapAfterAccelerationTime << "sec\n"; 
    std::cout << "areCompatible:\t" << areCompatibleTime << "sec\n"; 
    std::cout << "getBestFitVelocityAndAcceleration:\t" << getBestFitVelocityAndAccelerationTime << "sec\n";
    std::cout << "getBestFitVelocityAndAccelerationForTracklets:\t" << getBestFitVelocityAndAccelerationForTrackletsTime << "sec\n"; 
    std::cout << "addBestCompatibleTrackletsAndDetectionsToTrack:\t" << addBestCompatibleTrackletsAndDetectionsToTrackTime << "sec\n"; 
    std::cout << "trackMeetsRequirements:\t" << trackMeetsRequirementsTime << "sec\n"; 
    std::cout << "endpointTrackletsAreCompatible:\t" << endpointTrackletsAreCompatibleTime << "sec\n"; 
    std::cout << "buildTracksAddToResults:\t" << buildTracksAddToResultsTime << "sec\n"; 
    std::cout << "areAllLeaves:\t" << areAllLeavesTime << "sec\n"; 
    std::cout << "nodeWidth:\t" << nodeWidthTime << "sec\n"; 
    std::cout << "addAllDetectedObjectsToSet:\t" << addAllDetectedObjectsToSetTime << "sec\n"; 
    std::cout << "doLinkingRecurse:\t" << doLinkingRecurseTime << "sec\n";

    std::cout << "\n\nVisited doLinkingRecurse " << doLinkingRecurseVisits  << " times.\n";
    std::cout << "Visited buildTracksAddToResults " << buildTracksAddToResultsVisits << " times.\n";
    std::cout << "found " << compatibleEndpointsFound << " compatible endpoint pairs.\n";
    std::cout << "generated " << results.size() << " Tracks.\n";

}








std::vector <Track> 
linkTracklets(const std::vector<MopsDetection> &allDetections,
	      std::vector<Tracklet> &queryTracklets,
              linkTrackletsConfig searchConfig) {
    std::vector<Track> toRet;
    /*create a sorted list of KDtrees, each tree holding tracklets
      with unique start times (times of first detection in the tracklet).

      the points in the trees are in [RA, Dec, RAVelocity, DecVelocity] and the
      returned keys are indices into queryTracklets.
    */
    std::cout << "setting velocities of tracklets.\n";
    setTrackletVelocities(allDetections, queryTracklets);
    std::cout << "Building trees on tracklets.\n";
    std::map<double, KDTree::KDTree <unsigned int> > trackletTimeToTreeMap;    
    makeTrackletTimeToTreeMap(allDetections, queryTracklets, trackletTimeToTreeMap);
    std::cout << "Beginning the linking process.\n";
    doLinking(allDetections, queryTracklets, searchConfig, trackletTimeToTreeMap, toRet);
    
    return toRet;
}
