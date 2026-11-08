#!/bin/sh
# 启动重编版守护进程（8080），日志写 /tmp/nx-test.log
APP=/mnt/mmc/remote
NAME=nx-remote-controller-daemon-test
LOG=/tmp/nx-test.log

SELF_DIR=$(cd "$(dirname "$0")" 2>/dev/null && pwd)
BIN=""
for d in "$SELF_DIR" "$APP" /sdcard /mnt/mmc /tmp; do
    [ -z "$d" ] && continue
    if [ -f "$d/$NAME" ]; then BIN="$d/$NAME"; break; fi
done
[ -z "$BIN" ] && { echo "找不到 $NAME"; exit 1; }

for d in /proc/[0-9]*; do
    [ -r "$d/cmdline" ] || continue
    cmd=$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null)
    case "$cmd" in
        *nx-remote-controller-daemon-test*|*nx-remote-controller-daemon-debug*)
            kill -9 "${d#/proc/}" 2>/dev/null ;;
    esac
done
sleep 1

if netstat -an 2>/dev/null | grep -q ':8080 .*LISTEN'; then
    echo "8080 仍被占用，先跑 daemon-test-stop.sh"
    exit 1
fi

chmod +x "$BIN" 2>/dev/null
rm -f "$LOG"
APP_PATH=$APP NX_HTTP_PORT=8080 "$BIN" >"$LOG" 2>&1 &
sleep 2

PID=""
for d in /proc/[0-9]*; do
    [ -r "$d/cmdline" ] || continue
    cmd=$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null)
    case "$cmd" in
        *nx-remote-controller-daemon-test*) PID=${d#/proc/} ;;
    esac
done

if [ -z "$PID" ]; then
    echo "启动失败，日志："
    tail -20 "$LOG"
    exit 1
fi

echo "OK: pid=$PID 端口 8080, 日志 $LOG"