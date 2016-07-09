#!/bin/bash

set -e # exit on error

ino clean
echo Build the sketch...
ino build -m micro

## please check serial to avoid overrinding wrong board, esp. the heat control one.
for arduino in /dev/ttyACM* ;
do
echo upload to $arduino
ino upload -m micro -p $arduino
echo wait a little bit...
sleep 1
done
