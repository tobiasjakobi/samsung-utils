#!/bin/bash

EXYNOS_3250="3250"
EXYNOS_4x12="4x12"
OUT_DIR="out"

formats=(FMT_JPEG
	FMT_RGB565
	FMT_RGB565X
	FMT_RGB32
	FMT_BGR32
	FMT_YUYV
	FMT_YVYU
	FMT_UYVY
	FMT_VYUY
	FMT_NV24
	FMT_NV42
	FMT_NV16
	FMT_NV61
	FMT_NV12
	FMT_NV21
	FMT_YUV420
	FMT_GREY
)

num_formats=${#formats[@]}
for ((i=0; i < $num_formats; i++)); do
    name=${formats[i]}
    declare -r ${name}=$i
done

formats_4x12=($FMT_JPEG
	 $FMT_RGB565
	 $FMT_RGB32
	 $FMT_YUYV
	 $FMT_YVYU
	 $FMT_NV24
	 $FMT_NV42
	 $FMT_NV16
	 $FMT_NV61
	 $FMT_NV12
	 $FMT_NV21
	 $FMT_YUV420
	 $FMT_GRAY)

formats_3250=($FMT_JPEG
	 $FMT_RGB565
	 $FMT_RGB565X
	 $FMT_RGB32
	 $FMT_YUYV
	 $FMT_YVYU
	 $FMT_UYVY
	 $FMT_VYUY
	 $FMT_NV12
	 $FMT_NV21
	 $FMT_YUV420)

if [ "$1" == $EXYNOS_3250 ]
then
	test_formats=( "${formats_3250[@]}" )
	echo "Running tests for Exynos3250"
else
	test_formats=( "${formats_4x12[@]}" )
	echo "Running tests for Exynos4x12"
fi

rm -r $OUT_DIR
mkdir $OUT_DIR

CNT=1
JPEG_ENC_NODE=`dmesg | awk '/s5p-jpeg.*encoder/ {print substr($0, length($0), 1)}'`
JPEG_DEC_NODE=`dmesg | awk '/s5p-jpeg.*decoder/ {print substr($0, length($0), 1)}'`

while [ $CNT -ne ${#test_formats[@]} ]
do
	fmt_name=${formats[${test_formats[$CNT]}]}
	echo "============== jpeg -> "$fmt_name" =============="

	./test-jpeg -m1 -f$2 -oout.raw -v$JPEG_DEC_NODE -r${test_formats[$CNT]} -s$3 | tee out_dec
	JPEG_WIDTH=`cat out_dec | awk '/input JPEG dimensions/ {print $4}' | awk -Fx '{print $1}'`
	JPEG_HEIGHT=`cat out_dec | awk '/input JPEG dimensions/ {print $4}' | awk -Fx '{print $2}'`
	RAW_WIDTH=`cat out_dec | awk '/output image dimensions/ {print $4}' | awk -Fx '{print $1}'`
	RAW_HEIGHT=`cat out_dec | awk '/output image dimensions/ {print $4}' | awk -Fx '{print $2}'`
	SCALED_WIDTH=`cat out_dec | awk '/active area dimensions/ {print $4}' | awk -Fx '{print $1}'`
	SCALED_HEIGHT=`cat out_dec | awk '/active area dimensions/ {print $4}' | awk -Fx '{print $2}'`
	OUTPUT_FOURCC=`cat out_dec | awk '/output format set/ {print $4}' | awk -F_ '{print $4}'`
	OUTFORM=FMT_${OUTPUT_FOURCC}
	JPEG_SIZE=$JPEG_WIDTH"_"$JPEG_HEIGHT
	FMT_DESC=$(printf "%s_%sx%s" $fmt_name $SCALED_WIDTH $SCALED_HEIGHT)
	mv out.raw $OUT_DIR/out_$JPEG_SIZE-$FMT_DESC.raw

	echo "============== "$fmt_name" -> jpeg =============="
	./test-jpeg -m0 -f$OUT_DIR/out_$JPEG_SIZE-$FMT_DESC.raw -o$OUT_DIR/out_$JPEG_SIZE-$FMT_DESC.jpeg -w$RAW_WIDTH -h$RAW_HEIGHT -v$JPEG_ENC_NODE -r${!OUTFORM} -c0 -p0 -l0 -t0 -x$SCALED_WIDTH -y$SCALED_HEIGHT
	rm $OUT_DIR/out_$JPEG_SIZE-$FMT_DESC.raw
	(( CNT++ ))
done

tar czf out.tar.gz ./out
