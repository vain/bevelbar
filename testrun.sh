#!/bin/bash

make || exit 1

while sleep 1
do
    echo 0
    echo 0"$(date)"
    echo 1i am pressed
    echo 2i am urgent
    echo 3i am urgent and pressed
    echo -
    echo "0<-- that's an empty segment"
    echo e

    echo 1
    echo 0"$(date +%s) hähä"
    echo e

    echo f

#    echo a
#    echo 0"$(date)"
#    echo -
#    echo 0"$(xprop -root WM_NAME | sed 's/^[^"]\+"//; s/"$//')"
#    echo e
#    echo f
done | ./bevelbar left top 5 2 'Terminus:pixelsize=10' \
    '#a3a3a3' \
    '#b1b1b1' '#363636' \
    '#bebebe' '#000000' '#e1e1e1' '#747474' \
    '#bebebe' '#000000' '#747474' '#e1e1e1' \
    '#a11212' '#ffffff' '#e61919' '#961111' \
    '#f21b1b' '#ffffff' '#961111' '#e61919'
