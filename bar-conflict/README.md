# bar-conflict

Kernel module that claims a PCI BAR memory region using
`request_mem_region()`, creating a resource conflict. When vfio-pci
subsequently binds to the device, `pci_request_selected_regions()` fails
for that BAR.

Used by `iommufd-dmabuf-validate.sh` to test that vfio-pci rejects
DMABUF export for BARs whose resources could not be claimed.

## Build

```
make
```

## Usage

```
insmod bar-conflict.ko bdf=0000:31:00.0 bar=0
# bind device to vfio-pci, run test
rmmod bar-conflict
```

## Parameters

- `bdf` - PCI device address (ssss:bb:dd.f)
- `bar` - BAR index (0-5)
