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
  echo "usage: $0 [-c] [-h]"
  echo "-c: clean"
  echo "-h: help"
  cd "$CURRENT"
  exit
elif [ "$1" = "-c" -o "$1" = "--clean" -o "$1" = "-clean" -o "$1" = "clean" ]; then
  rm -rf bin
  python scripts/build.py --clean 1>/dev/null
  cd "$CURRENT"
  exit
else
  rm -rf bin
  echo "Building ..."
  {
    python scripts/build.py
  } || {
    echo "Failed to build :-("
    cd "$CURRENT"
    exit
  }
fi
echo "Finished"
cd "$CURRENT"
