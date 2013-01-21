#!/bin/sh -u
#
# Copyright (C) 2012 Codership Oy <info@codership.com>
#
# This is a script for glbd exec watchdog backend to monitor the availability
# of HTTP server.
#
# It tries to query the server for a given page, and echoes status back to glbd.
#
# Example usage:
# ./glbd -w exec:"curl.sh http://index.html" -t 2 80 192.168.0.1 192.168.0.2
#
# GLB will insert the address (host:port) of particular server between curl.sh
# and the rest of the command line, so that server address is the first
# argument for the script.
#
# The rest of the command line is the options passed to curl command as is.
#

# Server address
ADDR=$1
shift

# Place all arguments except for the last one in ARGS var
ARGS=
while [ $# -gt 1 ]
do
    ARGS="$ARGS '$1'"
    shift
done

# Last argument is URL, insert server address after ://
URL=$(echo $1 | sed s/\\:\\/\\//\\:\\/\\/$ADDR\\//)

while read CMD
do
    [ "$CMD" != "poll" ] && break;

    curl -qs $ARGS $URL > /dev/null

    if [ $? -eq 0 ]
    then
        STATE=3
    else
        STATE=0
    fi

    echo "$STATE"
done

