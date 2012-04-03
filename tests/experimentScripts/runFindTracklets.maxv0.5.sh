#!/bin/bash


FINDTRACKLETS=$MOPS_DAYMOPS_DIR/bin/findTracklets

NIGHTLY_DIASOURCES=$PWD/../*miti
OUTPUT_DIR=$PWD

echo "Placing output data in $OUTPUT_DIR"

for NIGHTLY in $NIGHTLY_DIASOURCES
do
    CMD="$FINDTRACKLETS -i $NIGHTLY -o $OUTPUT_DIR/`basename $NIGHTLY .miti`.maxv0.5.tracklets -v .5 -m 0.0"
    echo running $CMD
    /usr/bin/time -o $OUTPUT_DIR/`basename $NIGHTLY .miti`.maxv0.5.time $CMD
    echo ""
    echo ""
done