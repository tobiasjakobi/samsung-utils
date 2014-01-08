#!/bin/bash

formats=(FMT_JPEG
	 FMT_RGB565
         FMT_RGB32
         FMT_YUYV
         FMT_YVYU
         FMT_NV24
         FMT_NV42
         FMT_NV16
         FMT_NV61
         FMT_NV12
         FMT_NV21
         FMT_YUV420
	 FMT_GRAY)

CNT=1
JPEG_ENC_NODE=`dmesg | awk '/s5p-jpeg.*encoder/ {print substr($0, length($0), 1)}'`
JPEG_DEC_NODE=`dmesg | awk '/s5p-jpeg.*decoder/ {print substr($0, length($0), 1)}'`

while [ $CNT -ne ${#formats[@]} ]
do
	echo "============== jpeg -> "${formats[$CNT]}" =============="

	./test-jpeg -m1 -f$1 -oout_${formats[$CNT]}.raw -v$JPEG_DEC_NODE -r$CNT | tee out_dec
	WIDTH=`cat out_dec | awk '/output image dimensions/ {print $4}' | awk -Fx '{print $1}'`
	HEIGHT=`cat out_dec | awk '/output image dimensions/ {print $4}' | awk -Fx '{print $2}'`
	echo "parsed: "$WIDTH"x"$HEIGHT

	echo "============== "${formats[$CNT]}" -> jpeg =============="
	./test-jpeg -m0 -fout_${formats[$CNT]}.raw -oout_${formats[$CNT]}.jpeg -w$WIDTH -h$HEIGHT -v$JPEG_ENC_NODE -r$CNT -c0 -p0
	(( CNT++ ))
done
