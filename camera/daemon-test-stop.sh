#!/bin/sh
# 按 /proc/PID/cmdline 杀进程
# 为什么不用 killall：进程名超过 15 字符会被截断
# 为什么不用 /proc/PID/exe：用 mv 覆盖过的二进制会变成 "path (deleted)"
# cmdline 里保存的是启动时的完整参数，不受上面两个问题影响

killed=0
for d in /proc/[0-9]*; do
    [ -r "$d/cmdline" ] || continue
    cmd=$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null)
    case "$cmd" in
        *nx-remote-controller-daemon-test*|*nx-remote-controller-daemon-debug*)
            pid=${d#/proc/}
            echo "kill $pid : $cmd"
            kill -9 "$pid" 2>/dev/null
            killed=$((killed+1))
            ;;
    esac
done

sleep 1

if netstat -an 2>/dev/null | grep -q ':8080 .*LISTEN'; then
    echo "警告: 8080 仍被占用，占用者："
    for d in /proc/[0-9]*; do
        [ -r "$d/cmdline" ] || continue
        cmd=$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null)
        echo "  ${d#/proc/} : $cmd"
    done | grep -i daemon
else
    echo "已清理 $killed 个进程，8080 已释放"
fi