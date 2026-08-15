#!/bin/bash

me=${0##*/}

device=${1:-"0000:08:10.0"}

sysdev=/sys/bus/pci/devices/$device

if [ ! -e "$sysdev" ]; then
    echo "$me: device $device not found"
    exit 1
fi

if [ ! -d "$sysdev/vfio-dev" ]; then
    driver=$(basename $(readlink "$sysdev/driver" 2>/dev/null) 2>/dev/null)
    echo "$me: device $device is not bound to a vfio driver (driver: ${driver:-none})"
    exit 1
fi

detect_iommu() {
    # IOMMU address space size from CAP register (MGAW field, bits 21:16)
    iommu_cap=$(cat /sys/bus/pci/devices/$device/iommu/intel-iommu/cap 2>/dev/null)
    if [ -n "$iommu_cap" ]; then
	mgaw=$(( (0x$iommu_cap >> 16) & 0x3f ))
	echo "IOMMU address space: $(( mgaw + 1 ))-bit"
    fi

    # stress-test uses 2MB chunks over 1GB iterations: 512 entries per iteration
    dma_entry_limit=$(cat /sys/module/vfio_iommu_type1/parameters/dma_entry_limit 2>/dev/null)
    dma_entry_limit=${dma_entry_limit:-65535}
    map_max=$(( dma_entry_limit / 512 ))
    [ $map_max -lt 4 ] && map_max=4
    echo "DMA entry limit: $dma_entry_limit (stress-test: ${map_max} iterations)"
}

setup_hugepages() {
    hugepages_free=$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/free_hugepages 2>/dev/null)
    hugepages_free=${hugepages_free:-0}
    if [ $hugepages_free -eq 0 ]; then
	echo "Allocating hugepages..."
	echo 2560 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
	hugepages_free=$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/free_hugepages)
    fi
    echo "Hugepages free: $hugepages_free"
}

detect_iommu
setup_hugepages

logfile="run-test.log"
> "$logfile"

pass=0
fail=0
skip=0
failures=""

run_test() {
    name=$1
    echo "--- $name ---" >> "$logfile"
    "$@" >> "$logfile" 2>&1
    rc=$?
    if [ $rc -eq 0 ]; then
	pass=$((pass + 1))
	printf "  %-45s PASS\n" "$name"
    elif [ $rc -eq 77 ]; then
	skip=$((skip + 1))
	printf "  %-45s SKIP\n" "$name"
    else
	fail=$((fail + 1))
	failures="$failures $name"
	printf "  %-45s FAIL (exit code $rc)\n" "$name"
    fi
}

# Device open (legacy group API)
run_test ./vfio-pci-device-open $device
run_test ./vfio-pci-device-open-sparse-mmap $device
run_test ./vfio-pci-device-open-igd $device
run_test ./vfio-noiommu-pci-device-open $device

# Device open (iommufd cdev API)
run_test ./iommufd-pci-device-open $device

# DMA mapping (iommufd)
run_test ./iommufd-dma-map-unmap $device 256 5

# DMA-BUF export (iommufd)
run_test ./iommufd-dmabuf $device

# DMA mapping
run_test ./vfio-pci-device-dma-map $device
run_test ./vfio-pci-device-map-alignment $device
run_test ./vfio-correctness-tests $device /dev/hugepages
run_test ./vfio-iommu-map-unmap $device 255 40
run_test ./vfio-iommu-stress-test $device $map_max

# BAR fault timing
run_test ./vfio-pci-bar-fault-timing $device

# Hugepage guest mapping (PF only, VF auto-skips)
run_test ./vfio-huge-guest-test $device /dev/hugepages 8
run_test ./vfio-pci-huge-fault-race $device

# Hot reset (PF only, VF auto-skips)
run_test ./vfio-pci-hot-reset $device

# Migration
run_test ./vfio-pci-device-migration $device
run_test ./vfio-pci-device-migration-stress $device

echo ""
total=$((pass + fail + skip))
echo "$total tests: $pass passed, $fail failed, $skip skipped"
if [ -n "$failures" ]; then
    echo "Failures:$failures"
fi
exit $fail
