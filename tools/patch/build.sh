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
echo "Building ..."
{
	python scripts/build.py 1>/dev/null
} || {
	echo "Failed to build :-("
	cd "$CURRENT"
	exit
}
echo "Finished"
cd "$CURRENT"
