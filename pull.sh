#!/usr/bin/bash

TARGET_DIR=$1
CHOSEN_EXPORT=$2
SOURCE_EXPORT=$3

VIPURL='https://www.vipvgm.net/'
VIP='roster.min.json'

for URL in $(curl --silent "$VIPURL$VIP" \
             | ./vip-pull $TARGET_DIR "$CHOSEN_EXPORT" "$SOURCE_EXPORT")
do
        echo
        echo "downloading: $URL"
        curl --output-dir "$TARGET_DIR" -O "$URL"
done
echo
