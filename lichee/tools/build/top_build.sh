#!/bin/bash

cat .buildconfig | grep sun50i
if [ $? -eq 0 ]; then
	tools/build/mkcommon.sh $@
else
	buildroot/scripts/mkcommon.sh $@
fi

