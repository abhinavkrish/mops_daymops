// -*- LSST-C++ -*-


/* jonathan myers this is an attempt at doing removal of subset tracklets in a
 * much faster way than the naive algorithm.*/

#ifndef LSST_REMOVE_SUBSETS_H
#define LSST_REMOVE_SUBSETS_H

#include <vector>

#include "TrackletCollapser.h"
#include "../Tracklet.h"

namespace removeSubsets {


    class SubsetRemover {
    public:
        void removeSubsetsPopulateOutputVector(
            const std::vector<Tracklet> *pairsVector, 
            std::vector<Tracklet> &outVector);
    };

    void putLongestPerDetInOutputVector(const std::vector<Tracklet> *pairsVector, 
                                        std::vector<Tracklet> &outputVector);

    int removeSubsetsMain(int argc, char** argv);
    bool guessBoolFromStringOrGiveErr(std::string guessStr, std::string errStr);
}



#endif
