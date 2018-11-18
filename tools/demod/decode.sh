#!/bin/bash

file=$1
frq=$2
rate=$3
arrow=$4

rate1=$(($frq*4))
rate2=$(($frq*2))
rate3=$(($rate*16))

gcc demod.c -o demod
gcc uart.c -o uart


sox $file -r $(($frq*4)) -t dat - | tail -n +3 | ./demod | sox -t dat -r $rate2 -c 1 - -r $rate3 -t dat - sinc 0-$rate | tail -n +3 | ./uart $arrow
