#!/bin/bash

sudo rm -rf `pwd`/build/*
cd `pwd`/build
sudo cmake ..
sudo make
