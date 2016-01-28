#!/bin/bash

while sleep 1
do
    echo 0                                # Monitor 0, see note below
    echo 0"$(date +%s)"                   # Select style 0, some text
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

    # Note: Instead of a monitor index, you can also use "a" to show the
    # same text on all monitors.
done | ./bevelbar \
    left bottom \
    10 5 \
    3 2 \
    0.25 0.5 \
    'Terminus:pixelsize=10' \
    '#a3a3a3' \
    '#b1b1b1' '#363636' \
    '#bebebe' '#000000' '#e1e1e1' '#747474' \
    '#bebebe' '#000000' '#747474' '#e1e1e1' \
    '#a11212' '#ffffff' '#e61919' '#961111'

# Arguments:
#
# "left", "bottom"      Horizontal and vertical position (one of "left",
#                       "center", "right" and "top", "bottom")
#
# "10", "5"             Horizontal and vertical margin (distance to screen edge)
#
# "3", "2"              Size of outer and inner bevel
#
# "0.25", "0.5"         Factors to define the size of space between segments
#                       and of empty segments. Will be multiplied by font
#                       height.
#
# "Terminus..."         Font, obviously.
#
# "#a3a3a3"             Color of empty segments.
#
# "#b1b1b1", "#363636"  Bright and dark color of outer bevel border.
#
# group of 4 colors     These are the user defined styles which you can
#                       select in your input data (see above). First
#                       color is background color, second color is text
#                       color, third color is this segment's bright
#                       bevel color, fourth color is the dark bevel
#                       color.
#
# Please note: *ALL* of these arguments and at least one style
# description have to be given.
