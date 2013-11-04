#!/bin/bash

trap exit INT

SUCCESS=1
until "$@"
do
	echo "Retrying..."
done
