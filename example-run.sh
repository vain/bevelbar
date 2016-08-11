#!/bin/bash

while sleep 0.25
do
    echo 0                                # Monitor 0
    echo 0"$(date +%s)"                   # Select style 0
    echo 1I am pressed                    # Select style 1
    echo 2This is another style
    echo -                                # Request an empty segment
    echo "0<-- that's an empty segment"
    echo e                                # End input for selected monitor

    echo 1
    echo 0"$(date)"
    echo e

    echo 2
    echo 0I am on monitor number 2
    echo e

    echo f
done | ./bevelbar \
    -h left -v top \
    -H 10 -V 5 \
    -b 3 -B 2 \
    -m 2 -e 20 \
    -f 'Terminus:pixelsize=10' \
    -p '#a3a3a3' \
    -o '#e1e1e1' -O '#262626' \
    -s 0 -c '#bebebe' -c '#000000' -c '#e1e1e1' -c '#747474' \
    -s 1 -c '#bebebe' -c '#000000' -c '#747474' -c '#e1e1e1' \
    -s 2 -c '#a11212' -c '#ffffff' -c '#e61919' -c '#570A0A'

# Note that all arguments to bevelbar are optional.
