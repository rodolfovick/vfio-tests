# VFIO Test Suite

User-space test programs for the Linux VFIO and IOMMUFD subsystems.
Exercises PCI device open, DMA mapping, hot reset, hugepage fault
handling, and live migration through the VFIO kernel interfaces.

## Building

    make

## Running

Tests require root and a PCI device bound to a VFIO driver
(`vfio-pci`).  Bind the device, then:

    make check DEVICE=0000:08:10.0

or directly:

    ./run-test.sh 0000:08:10.0

The runner allocates 2 MB hugepages, detects IOMMU limits, and reports
a pass/fail/skip summary.

Set `VFIO_VERBOSE=1` for detailed output.

## Tests

| Test | Area |
|------|------|
| `vfio-pci-device-open` | Device open (legacy group API) |
| `vfio-pci-device-open-sparse-mmap` | Sparse mmap regions |
| `vfio-pci-device-open-igd` | Intel IGD-specific open |
| `vfio-noiommu-pci-device-open` | No-IOMMU mode |
| `iommufd-pci-device-open` | Device open (IOMMUFD cdev API) |
| `vfio-pci-device-dma-map` | DMA mapping basics |
| `vfio-pci-device-map-alignment` | BAR mmap alignment |
| `vfio-pci-bar-fault-timing` | BAR fault timing (PTE/PMD/PUD) |
| `vfio-correctness-tests` | DMA correctness with hugepages |
| `vfio-iommu-map-unmap` | IOMMU map/unmap cycles |
| `vfio-iommu-stress-test` | IOMMU mapping stress |
| `vfio-huge-guest-test` | Hugepage guest mappings |
| `vfio-pci-huge-fault-race` | Hugepage fault races |
| `vfio-pci-hot-reset` | PCI hot reset |
| `vfio-pci-device-migration` | Live migration |
| `vfio-pci-device-migration-stress` | Migration stress |

## License

GPLv2 -- see `COPYING`.
