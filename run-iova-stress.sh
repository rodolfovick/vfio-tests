#!/bin/bash
#
# IOVA stress test - exercises large IOVA ranges using DMA map/unmap tests
#
# Similar to iova_stress.c by Pasha Tatashin: 4KB chunks with 2MB IOVA
# stride across a large address space. Default 45TB like iova_stress.
#
# See https://github.com/soleen/iova_stress
#
# Requires ~90GB of mmap'd memory for 45TB (23.6M chunks * 4KB).
#

if [ -z "$1" ]; then
	echo "usage: $0 <ssss:bb:dd.f> [iova_tb]"
	exit 1
fi

BDF=$1
IOVA_TB=${2:-45}

# 4KB chunks, 2MB stride like iova_stress
# nr_chunks = iova_tb * 1TB / 2MB
# map_size_mb = nr_chunks * 4KB / 1MB
MAP_SIZE_MB=$(( IOVA_TB * 1024 * 1024 / 2 * 4 / 1024 ))
NR_CHUNKS=$(( MAP_SIZE_MB * 1024 / 4 ))

echo "IOVA stress: ${IOVA_TB}TB range, 2MB stride, map_size=${MAP_SIZE_MB}MB, ${NR_CHUNKS} mappings"
echo

DMA_LIMIT=/sys/module/vfio_iommu_type1/parameters/dma_entry_limit
SAVED_LIMIT=$(cat "$DMA_LIMIT")

echo "=== legacy container ==="
echo "$NR_CHUNKS" > "$DMA_LIMIT"
time ./vfio-iommu-map-unmap "$BDF" "$MAP_SIZE_MB" 1 2048
echo "$SAVED_LIMIT" > "$DMA_LIMIT"
echo

echo "=== iommufd ==="
time ./iommufd-dma-map-unmap "$BDF" "$MAP_SIZE_MB" 1 2048
