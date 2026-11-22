#!/bin/sh
# 合并版启动脚本：先保留 nx-remote-controller 通道，再后台启动 nx300m-mod
# 原版备份：/mnt/mmc/autoexec.sh.nxremote-bak

# ---------- 1. 原有 nx-remote-controller 通道 ----------
mkdir -p /dev/pts
mount -t devpts none /dev/pts

inetd /mnt/mmc/remote/inetd.conf

/mnt/mmc/remote/start.sh
/mnt/mmc/remote/connect.sh

# ---------- 2. nx300m-mod（后台运行，避免阻塞启动） ----------
if [ ! -e /mnt/mmc/nx300m-mod/DISABLED ]; then
(
    PATH=/bin:/usr/bin:/sbin:/usr/sbin
    export PATH
    export NX_REMOTE_LISTEN=0.0.0.0:8781
    export NX_REMOTE_CUSTOM_KEY=0 NX_REMOTE_PANEL_SHORTCUT=1
    export NX_REMOTE_MENU_KEY=0
    export NX_REMOTE_PANEL_KEY=custom
    unset LD_PRELOAD LD_AUDIT LD_LIBRARY_PATH

    [ -r /mnt/mmc/nx300m-lens-debug/autoexec-wifi.sh ] && \
        /bin/sh /mnt/mmc/nx300m-lens-debug/autoexec-wifi.sh

    if [ -r /mnt/mmc/nx300m-mod/mfw-service-entry.sh ]; then
        NX300M_MOD_ROOT=/mnt/mmc/nx300m-mod NX_MOD_ROOT=/mnt/mmc/nx300m-mod \
            /bin/sh /mnt/mmc/nx300m-mod/mfw-service-entry.sh
    fi
) &
fi


# ---------- 3. mjpeg_proxy (MJPEG preview proxy, optional) ----------
# Serves the viewer on :8081. Safe to disable:
#   touch /mnt/mmc/mjpeg_proxy.DISABLED
# Args: <listen port> <default scale> <default q> <fps, 0=unlimited> <daemon port>
if [ -x /mnt/mmc/mjpeg_proxy ] && [ ! -e /mnt/mmc/mjpeg_proxy.DISABLED ]; then
    (
        sleep 4
        cd /mnt/mmc
        exec ./mjpeg_proxy 8081 3 70 0 80
    ) >/tmp/mjpeg_proxy.log 2>&1 &
fi

exit 0