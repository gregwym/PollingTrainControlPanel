#!/bin/bash

cd ./io/src/
make clean
make
cp plio.a ../lib/libplio.a
cp bwio.a ../lib/libbwio.a
cd -
make clean
make
cp *.elf ~/cs452/tftp/
chmod a+r ~/cs452/tftp/*
