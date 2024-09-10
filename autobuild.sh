#!/bin/bash

sudo rm -rf `pwd`/build/*
rm -rf `pwd`/bin.server
cd `pwd`/build
sudo cmake ..
sudo make
