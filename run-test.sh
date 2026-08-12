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

# Hugepage availability
hugepages_free=$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/free_hugepages 2>/dev/null)
hugepages_free=${hugepages_free:-0}
echo "Hugepages free: $hugepages_free"

pass=0
fail=0
skip=0
failures=""

run_test() {
	echo "--- $1 ---"
	"$@"
	rc=$?
	name=$1
	if [ $rc -eq 0 ]; then
		pass=$((pass + 1))
		echo "PASS: $name"
	elif [ $rc -eq 77 ]; then
		skip=$((skip + 1))
		echo "SKIP: $name"
	else
		fail=$((fail + 1))
		failures="$failures $name"
		echo "FAIL: $name (exit code $rc)"
	fi
	echo ""
}

run_test ./vfio-correctness-tests $groupid

if [ $is_vf -eq 0 ]; then
	if [ $hugepages_free -eq 0 ]; then
		echo "Allocating hugepages for huge-guest-test..."
		echo 2048 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
		hugepages_free=$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/free_hugepages)
		echo "Hugepages free: $hugepages_free"
	fi
	run_test ./vfio-huge-guest-test $groupid /dev/hugepages 8
	run_test ./vfio-pci-hot-reset $device
	run_test ./vfio-pci-huge-fault-race $device
fi

run_test ./vfio-iommu-map-unmap $device $map_size_mb 5
run_test ./vfio-iommu-stress-test $device $map_max

run_test ./vfio-noiommu-pci-device-open $device
run_test ./vfio-pci-device-dma-map $device
run_test ./vfio-pci-device-open $device
run_test ./vfio-pci-device-open-igd $device
run_test ./vfio-pci-device-open-sparse-mmap $device
run_test ./iommufd-pci-device-open $device
run_test ./vfio-pci-device-migration $device
run_test ./vfio-pci-device-migration-stress $device
run_test ./vfio-pci-device-map-alignment $device

echo "=== Summary ==="
total=$((pass + fail + skip))
echo "$total tests: $pass passed, $fail failed, $skip skipped"
if [ -n "$failures" ]; then
	echo "Failures:$failures"
fi
exit $fail
