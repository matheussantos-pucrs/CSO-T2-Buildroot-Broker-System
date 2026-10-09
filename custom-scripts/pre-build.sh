#!/bin/sh

HOST=`ip route show | head --lines 1 | awk '{print $9}'`

mkdir -p $BASE_DIR/../overlay/etc/init.d/

cat $BASE_DIR/../custom-scripts/network-config | sed 's/\[IP-DO-HOST\]/'"$HOST"'/g' > $BASE_DIR/../overlay/etc/init.d/S41network-config

chmod +x $BASE_DIR/../overlay/etc/init.d/S41network-config

make -C $BASE_DIR/../apps/banner

make -C $BASE_DIR/../modules/hello

make -C $BASE_DIR/../modules/chardrv

make -C $BASE_DIR/../modules/timerdrv

make -C $BASE_DIR/../modules/pubsub
