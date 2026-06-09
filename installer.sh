#!/bin/bash

set -e

LIBEXEC_DIR="/usr/local/libexec/jebot"
SYSTEMD_DIR="/etc/systemd/system"

if [ "$(id -u)" -ne 0 ]; then
    echo "this script must be run as root." >&2
    exit 1
fi

case "$1" in
    install)
        mkdir -p "$LIBEXEC_DIR"
        cp -f bin/* "$LIBEXEC_DIR"/
        chmod +x "$LIBEXEC_DIR"/*
        
        cp -f systemd/jebot-*.service "$SYSTEMD_DIR"/
        systemctl daemon-reload
        for service in "$SYSTEMD_DIR"/jebot-*.service; do
            systemctl enable "$(basename $service)"
        done
        ;;
    uninstall)
        rm -rf "$LIBEXEC_DIR"

        for service in "$SYSTEMD_DIR"/jebot-*.service; do
            systemctl disable --now "$(basename $service)"
        done
        rm -f "$SYSTEMD_DIR"/jebot-*.service
        systemctl daemon-reload
        ;;
    *)
        echo -e "usage:\n\t$0 [install|uninstall]" >&2
        exit 2
        ;;
esac
