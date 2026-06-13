#!/bin/bash

set -e

LIBEXEC_DIR="/usr/local/libexec/jebot"
SHARE_DIR="/usr/local/share/jebot"
SYSTEMD_DIR="/etc/systemd/system"

DATA_DIR="$SHARE_DIR/data"

PYTHON_VENV="/usr/local/lib/jebot-venv"

MAP_DATA="$DATA_DIR"/map

STT_DATA="$DATA_DIR/stt"
STT_MODELS="$STT_DATA/models"

VOSK_MODEL_EN_URL="https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip"
VOSK_MODEL_EN_PATH="$STT_MODELS/en"

VOSK_MODEL_AR_URL="https://alphacephei.com/vosk/models/vosk-model-ar-mgb2-0.4.zip"
VOSK_MODEL_AR_PATH="$STT_MODELS/ar"

if [ "$(id -u)" -ne 0 ]; then
    echo "this script must be run as root." >&2
    exit 1
fi

case "$1" in
    install)
        # setup paths
        mkdir -p "$LIBEXEC_DIR" "$SHARE_DIR"

        mkdir -p "$DATA_DIR"

        mkdir -p "$MAP_DATA"

        mkdir -p "$STT_DATA" "$STT_MODELS"

        # copy files
        cp -f bin/* "$LIBEXEC_DIR"/
        cp -f python/stt/sttd.py "$LIBEXEC_DIR"/
        chmod +x "$LIBEXEC_DIR"/*

        cp -f data/* "$DATA_DIR"/

        # python venv
        [ ! -d "$PYTHON_VENV" ] && python3 -m venv "$PYTHON_VENV"
        PYTHON_JEBOT_LIB="$($PYTHON_VENV/bin/python3 -c 'import site; print(site.getsitepackages()[0])')"/jebot
        "$PYTHON_VENV"/bin/pip install -r python/stt/requirements.txt
        "$PYTHON_VENV"/bin/pip install -r python/map/requirements.txt

        # python jebot lib
        mkdir -p "$PYTHON_JEBOT_LIB"
        touch "$PYTHON_JEBOT_LIB"/__init__.py

        cp -f python/{stt,map}/lib/* "$PYTHON_JEBOT_LIB"/

        cp -f python/stt/data/* "$STT_DATA"/
        cp -f python/map/data/* "$MAP_DATA"/

        # download english vosk model if not exists
        if [ ! -d "$VOSK_MODEL_EN_PATH" ]; then
            mkdir -p "$VOSK_MODEL_EN_PATH"
            wget -O /tmp/vosk-en.zip "$VOSK_MODEL_EN_URL"
            unzip /tmp/vosk-en.zip -d /tmp/vosk-extract
            mv /tmp/vosk-extract/*/* "$VOSK_MODEL_EN_PATH"/
            rm -rf /tmp/{vosk-en.zip,vosk-extract}
        fi

        # download arabic vosk model if not exists
        if [ ! -d "$VOSK_MODEL_AR_PATH" ]; then
            mkdir -p "$VOSK_MODEL_AR_PATH"
            wget -O /tmp/vosk-ar.zip "$VOSK_MODEL_AR_URL"
            unzip /tmp/vosk-ar.zip -d /tmp/vosk-extract
            mv /tmp/vosk-extract/*/* "$VOSK_MODEL_AR_PATH"/
            rm -rf /tmp/{vosk-ar.zip,vosk-extract}
        fi

        # systemd
        cp -f systemd/jebot-*.service "$SYSTEMD_DIR"/
        systemctl daemon-reload
        for service in "$SYSTEMD_DIR"/jebot-*.service; do
            systemctl enable --now "$(basename $service)"
        done
        ;;
    uninstall)
        rm -rf "$LIBEXEC_DIR"
        rm -rf "$SHARE_DIR"
        rm -rf "$PYTHON_VENV"

        for service in "$SYSTEMD_DIR"/jebot-*.service; do
            systemctl disable --now "$(basename $service)"
        done
        rm -f "$SYSTEMD_DIR"/jebot-*.service
        systemctl daemon-reload
        ;;
    *)
        echo -e "usage:\n\t$0 [install|uninstall]" >&2
        exit 1
        ;;
esac
