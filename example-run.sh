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
    left bottom \
    10 5 \
    3 2 \
    2 20 \
    'Terminus:pixelsize=10' \
    '#a3a3a3' \
    '#e1e1e1' '#262626' \
    '#bebebe' '#000000' '#e1e1e1' '#747474' \
    '#bebebe' '#000000' '#747474' '#e1e1e1' \
    '#a11212' '#ffffff' '#e61919' '#570A0A'
