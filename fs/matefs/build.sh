#!/bin/bash

SYSTEM=`uname`
if [ "$SYSTEM" = "Linux" ]; then
  FILE=`readlink -f $0`
else
  echo "Error: unknown system"
  exit
fi
HOME=`dirname "$FILE"`
CURRENT=`pwd`
cd "$HOME"
if [ "$1" = "-h" -o "$1" = "--help" -o "$1" = "-help" ]; then
  echo "usage: $0 [-c]"
  echo "-c: clean"
  echo "-h: help"
  cd "$CURRENT"
  exit
elif [ "$1" = "-c" -o "$1" = "--clean" -o "$1" = "-clean" -o "$1" = "clean" ]; then
  rm -rf build
  rm -rf scripts/*.pyc
  cd "$CURRENT"
  exit
else
  rm -rf build
    echo "Building ..."
    {
        python scripts/build.py 1>/dev/null
    } || {
        echo "Failed to build :-("
        cd "$CURRENT"
        exit
    }
fi
echo "Finished"
cd "$CURRENT"
