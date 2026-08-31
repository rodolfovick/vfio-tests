#!/bin/bash
#
# Validate DMABUF BAR resource checks in vfio-pci.
#
# Creates a BAR resource conflict using the bar-conflict kernel module
# before binding vfio-pci, so that pci_request_selected_regions()
# fails for the targeted BAR. On a fixed kernel, the DMABUF export is
# rejected. On an unfixed kernel, it succeeds anyway.
#
# The device must be bound to vfio-pci before running this script.
#
# Usage: ./iommufd-dmabuf-validate.sh <ssss:bb:dd.f> [bar]
#
# Requires: bar-conflict.ko (build with: make -C bar-conflict)
#

if [ -z "$1" ]; then
	echo "usage: $0 <ssss:bb:dd.f> [bar]"
	exit 1
fi

BDF=$1
BAR=${2:-0}
DIR=$(dirname "$0")
MODULE="$DIR/bar-conflict/bar-conflict.ko"

if [ ! -x "$DIR/iommufd-dmabuf" ]; then
	echo "iommufd-dmabuf not built, run make first"
	exit 1
fi

if [ ! -f "$MODULE" ]; then
	echo "bar-conflict.ko not built, run: make -C bar-conflict"
	exit 1
fi

SYSDEV=/sys/bus/pci/devices/$BDF
if [ ! -e "$SYSDEV" ]; then
	echo "device $BDF not found"
	exit 1
fi

DRIVER=$(basename $(readlink "$SYSDEV/driver" 2>/dev/null) 2>/dev/null)

if [ "$DRIVER" != "vfio-pci" ]; then
	echo "device $BDF is not bound to vfio-pci (driver: ${DRIVER:-none})"
	exit 1
fi

echo "=== unbinding $BDF from vfio-pci ==="
echo "$BDF" > /sys/bus/pci/drivers/vfio-pci/unbind

echo "=== claiming $BDF BAR$BAR ==="
insmod "$MODULE" bdf="$BDF" bar="$BAR"
if [ $? -ne 0 ]; then
	echo "failed to claim BAR$BAR, rebinding vfio-pci"
	echo "$BDF" > /sys/bus/pci/drivers/vfio-pci/bind
	exit 1
fi

echo "=== rebinding $BDF to vfio-pci ==="
echo "$BDF" > /sys/bus/pci/drivers/vfio-pci/bind

echo "=== iommufd-dmabuf ==="
"$DIR/iommufd-dmabuf" -b "$BAR" "$BDF"
RC=$?
echo

echo "=== unbinding $BDF from vfio-pci ==="
echo "$BDF" > /sys/bus/pci/drivers/vfio-pci/unbind

echo "=== releasing BAR$BAR ==="
rmmod bar-conflict

echo "=== rebinding $BDF to vfio-pci ==="
echo "$BDF" > /sys/bus/pci/drivers/vfio-pci/bind

exit $RC
