#!/bin/bash

me=${0##*/}

device=${1:-"0000:08:10.0"}
groupid=$(basename $(readlink /sys/bus/pci/devices/$device/iommu_group))

# Detect VF vs PF
if [ -e /sys/bus/pci/devices/$device/physfn ]; then
	is_vf=1
	echo "Device $device is a Virtual Function"
else
	is_vf=0
	echo "Device $device is a Physical Function"
fi

# IOMMU address space size from CAP register (MGAW field, bits 21:16)
iommu_cap=$(cat /sys/bus/pci/devices/$device/iommu/intel-iommu/cap 2>/dev/null)
if [ -n "$iommu_cap" ]; then
	mgaw=$(( (0x$iommu_cap >> 16) & 0x3f ))
	echo "IOMMU address space: $(( mgaw + 1 ))-bit"
fi

# DMA mapping limits from vfio_iommu_type1 dma_entry_limit
pagesize=$(getconf PAGESIZE)
dma_entry_limit=$(cat /sys/module/vfio_iommu_type1/parameters/dma_entry_limit 2>/dev/null)
dma_entry_limit=${dma_entry_limit:-65535}
# map-unmap uses page-sized chunks: max mappable = dma_entry_limit * pagesize
map_size_mb=$(( dma_entry_limit * pagesize / 1024 / 1024 ))
# stress-test uses 2MB chunks over 1GB iterations: 512 entries per iteration
map_max=$(( dma_entry_limit / 512 ))
[ $map_max -lt 4 ] && map_max=4
echo "DMA entry limit: $dma_entry_limit, page size: ${pagesize} (page map: ${map_size_mb}MB, 2M map: ${map_max} iterations)"

set -x

./vfio-correctness-tests $groupid

if [ $is_vf -eq 0 ]; then
	./vfio-huge-guest-test $groupid
	./vfio-pci-hot-reset $device
	./vfio-pci-huge-fault-race $device
fi

./vfio-iommu-map-unmap $device $map_size_mb 5
./vfio-iommu-stress-test $device $map_max

./vfio-noiommu-pci-device-open $device
./vfio-pci-device-dma-map $device
./vfio-pci-device-open $device
./vfio-pci-device-open-igd $device
./vfio-pci-device-open-sparse-mmap $device
./iommufd-pci-device-open $device
./vfio-pci-device-migration $device
./vfio-pci-device-migration-stress $device
./vfio-pci-device-map-alignment $device
