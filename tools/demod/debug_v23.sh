#!/bin/bash

dir_1k2=$1-out.wav
dir_75=$1-in.wav

tmp=`mktemp /tmp/XXXXXXX`
./decode.sh $dir_1k2 1700 1200 ">>>" >> $tmp
./decode.sh $dir_75  420  75 "<<<" >> $tmp

sort -n $tmp
rm $tmp
