#!/bin/bash

current_path=`pwd`
if [ ! -d $current_path/build ]; then
  mkdir -p $current_path/build
fi

if [ ! -z "$(ls -A $current_path/build)" ]; then
  sudo rm -rf $current_path/build/*
fi

if [ ! -z "$(ls -A $current_path/bin.server)" ]; then
  rm -rf $current_path/bin.server
fi

cd $current_path/build
sudo cmake ..
sudo make
