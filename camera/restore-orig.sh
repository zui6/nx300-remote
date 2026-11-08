#!/bin/sh
# restore-orig.sh —— 一条命令回到原版 daemon
# 用法: sh /mnt/mmc/remote/restore-orig.sh
cd /mnt/mmc/remote || exit 1
if [ ! -f nx-remote-controller-daemon.orig ]; then
    echo "ERROR: nx-remote-controller-daemon.orig missing"
    exit 1
fi
for p in /proc/[0-9]*; do
    c=$(tr '\0' ' ' < $p/cmdline 2>/dev/null)
    case "$c" in
        *nx-remote-controller-daemon*) pid=${p#/proc/}; echo "kill $pid"; kill -9 $pid ;;
    esac
done
sleep 1
rm -f nx-remote-controller-daemon
cp -f nx-remote-controller-daemon.orig nx-remote-controller-daemon
sync
md5sum nx-remote-controller-daemon
echo "restored; reboot the camera to start the original daemon"