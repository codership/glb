#!/bin/sh -u
#
# Copyright (C) 2012 Codership Oy <info@codership.com>
#
# This is a script for glbd exec watchdog backend to monitor the availability
# of MySQL server.
#
# It tries to query the server for particular status variables, and echoes
# information back to glbd.
#
# Example usage:
# ./glbd -w exec:"mysql.sh -d 2 -utest -ptestpass" -t 2 3306 192.168.0.1 192.168.0.2
#
# GLB will insert the address (host:port) of particular server between mysql.sh
# and the rest of the command line, so that server address is the first
# argument for the script.
#
# -d option (optional, should always be the first) specifies the state that
# should be assigned to the Galera donor node:
#
# 1 - forcefully cut all connections to it
# 2 - stop directing connections to it, but leave established ones intact
#     (default)
# 3 - treat the donor as a regular working node
#
# The rest of the command line is the options passed to mysql command as is.
#
#ADDR=(${1//:/ }) bash-specific
ADDR=$1
shift

DONOR_STATE=2
if [ "$1" = "-d" ]
then
    shift
    DONOR_STATE=$1
    shift
fi

#HOST=${ADDR[0]:-"127.0.0.1"}
#PORT=${ADDR[1]:-"3306"}
HOST=$(echo $ADDR | cut -d ':' -f 1)
HOST=${HOST:-"127.0.0.1"}
PORT=$(echo $ADDR | cut -s -d ':' -f 2)
PORT=${PORT:-"3306"}

QUERY="SHOW STATUS LIKE 'wsrep_local_state'; SHOW STATUS LIKE 'wsrep_incoming_addresses'"

while read CMD
do
    [ "$CMD" != "poll" ] && break;

    RES=$(mysql -B --disable-column-names -h$HOST -P$PORT $* -e "$QUERY")

    if [ $? -eq 0 ]
    then
        STATE=$(echo $RES | cut -d ' ' -f 2)
        OTHERS=$(echo $RES | cut -d ' ' -f 4)
    # If wsrep_local_state variable was not found on the server, we assume it
    # is a regular MySQL and is ready for connections (it accepted connection)
        STATE=${STATE:-"4"}
    else
        STATE=
        OTHERS=
    fi

    # convert wsrep state to glbd code
    case $STATE in
        4) STATE="3"
        ;;
        3) STATE="2"
        ;;
        2) STATE="$DONOR_STATE"
        ;;
        1|5) STATE="1"
        ;;
        0|*) STATE="0"
    esac

    echo "$STATE $OTHERS"
done

#echo "Got cmd: '$CMD', exiting." >&2
