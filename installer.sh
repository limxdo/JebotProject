#!/bin/bash

set -e

USAGE="usage:\n\t$0 [install|uninstall]"

PREFIX="/usr/local"
LIBEXECDIR="$PREFIX/libexec/jebot"

if [ "$(id -u)" -ne 0 ]; then
    echo "this script must be run as root." >&2
    exit 1
fi

case "$1" in
    install)
        mkdir -p "$LIBEXECDIR"
        cp -f bin/* "$LIBEXECDIR"/
        chmod +x "$LIBEXECDIR"/*
        ;;
    uninstall)
        rm -rf "$LIBEXECDIR"
        ;;
    *)
        echo -e "$USAGE" >&2
        exit 2
        ;;
esac
