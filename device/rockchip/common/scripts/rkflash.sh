#!/bin/bash

COMMON_DIR=$(cd "$(dirname "$(realpath "$0")")/.."; pwd)
SDK_DIR=$(realpath $COMMON_DIR/../../..)
UPGRADETOOL=$SDK_DIR/tools/linux/Linux_Upgrade_Tool/Linux_Upgrade_Tool/upgrade_tool
ROCKDEV_DIR=$SDK_DIR/rockdev
LOADER=$ROCKDEV_DIR/MiniLoaderAll.bin
PARAMETER=$ROCKDEV_DIR/parameter.txt
UBOOT=$ROCKDEV_DIR/uboot.img
BOOT=$ROCKDEV_DIR/boot.img
RECOVERY=$ROCKDEV_DIR/recovery.img
MISC=$ROCKDEV_DIR/misc.img
ROOTFS=$ROCKDEV_DIR/rootfs.img
UPDATE=$ROCKDEV_DIR/update.img

if [ ! -n "$1" ];then
echo "flash all images as default"
FLASH_TYPE=all
else
FLASH_TYPE="$1"
fi

if [ $FLASH_TYPE = all ]
then
	$UPGRADETOOL ul -noreset $LOADER
	$UPGRADETOOL di -p $PARAMETER
	$UPGRADETOOL di -uboot $UBOOT
	$UPGRADETOOL di -b $BOOT
	$UPGRADETOOL di -r $RECOVERY
	$UPGRADETOOL di -m $MISC
	$UPGRADETOOL di -rootfs $ROOTFS
	$UPGRADETOOL rd
fi

if [ $FLASH_TYPE = tb ]
then
	$UPGRADETOOL ul -noreset $LOADER
	$UPGRADETOOL di -p $PARAMETER
	$UPGRADETOOL di -uboot $UBOOT
	$UPGRADETOOL di -b $BOOT
	$UPGRADETOOL rd
fi

if [ $FLASH_TYPE = norecovery ]; then
	$UPGRADETOOL ul -noreset $LOADER
	$UPGRADETOOL di -p $PARAMETER
	$UPGRADETOOL di -uboot $UBOOT
	$UPGRADETOOL di -b $BOOT
	$UPGRADETOOL di -rootfs $ROOTFS
fi

if [ $FLASH_TYPE = loader ]
then
	if [ -n "$2" ];then
		LOADER=$2
	fi
	echo "flash loader: $LOADER"
	$UPGRADETOOL ul $LOADER
	exit 0
fi

if [ $FLASH_TYPE = parameter ]
then
	if [ -n "$2" ];then
		PARAMETER=$2
	fi
	echo "flash parameter: $PARAMETER"
	$UPGRADETOOL di -p $PARAMETER
fi

if [ $FLASH_TYPE = resource ]
then
	if [ -n "$2" ];then
		RESOURCE=$2
	fi
	echo "flash resource: $RESOURCE"
	$UPGRADETOOL di -resource $RESOURCE
fi

if [ $FLASH_TYPE = uboot ]
then
	if [ -n "$2" ];then
		UBOOT=$2
	fi
	echo "flash uboot: $UBOOT"
	$UPGRADETOOL di -uboot $UBOOT
fi

if [ $FLASH_TYPE = trust ]
then
	echo "flash trust is unsupported: current Topeet partition table uses rootfs:grow and has no trust partition"
	exit 1
fi

if [ $FLASH_TYPE = boot ]
then
	if [ -n "$2" ];then
		BOOT=$2
	fi
	echo "flash boot: $BOOT"
	$UPGRADETOOL di -b $BOOT
fi

if [ $FLASH_TYPE = recovery ]
then
	if [ -n "$2" ];then
		RECOVERY=$2
	fi
	echo "flash recovery: $RECOVERY"
	$UPGRADETOOL di -r $RECOVERY
fi

if [ $FLASH_TYPE = misc ]
then
	if [ -n "$2" ];then
		MISC=$2
	fi
	echo "flash misc: $MISC"
	$UPGRADETOOL di -misc $MISC
fi

if [ $FLASH_TYPE = oem ]
then
	echo "flash oem is unsupported: current Topeet partition table uses rootfs:grow and has no oem partition"
	exit 1
fi

if [ $FLASH_TYPE = userdata ]
then
	echo "flash userdata is unsupported: current Topeet partition table uses rootfs:grow and has no userdata partition"
	exit 1
fi

if [ $FLASH_TYPE = rootfs ]
then
	if [ -n "$2" ];then
		ROOTFS=$2
	fi
	echo "flash rootfs: $ROOTFS"
	$UPGRADETOOL di -rootfs $ROOTFS
fi

if [ $FLASH_TYPE = update ]
then
	$UPGRADETOOL uf $UPDATE
fi

if [ $FLASH_TYPE = rd ]
then
	$UPGRADETOOL rd
fi

if [ $FLASH_TYPE = erase ]
then
	$UPGRADETOOL EF $LOADER
fi
