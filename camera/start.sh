#!/bin/bash

killall nx-input-injector
killall xev-nx
killall nx-remote-controller-daemon

export APP_PATH=/mnt/mmc/remote

$APP_PATH/lcd_control.sh on
if [ "$1" == "debug" ]; then
    $APP_PATH/nx-remote-controller-daemon
else
    $APP_PATH/nx-remote-controller-daemon &> /dev/null &
    sleep 2
    RUNNING=0
    for p in /proc/[0-9]*; do
        c=$(tr '\0' ' ' < $p/cmdline 2>/dev/null)
        case "$c" in
            *nx-remote-controller-daemon*) RUNNING=1 ;;
        esac
    done
    if [ "$RUNNING" = "0" ] && [ -x $APP_PATH/nx-remote-controller-daemon.orig ]; then
        $APP_PATH/nx-remote-controller-daemon.orig &> /dev/null &
        echo "fallback: original daemon started"
    fi
fi

exit 0;