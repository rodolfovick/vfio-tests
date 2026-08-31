Name:           vfio-tests
Version:        0.1.0
Release:        1%{?dist}
Summary:        Validation tools for the Linux VFIO and IOMMUFD subsystems
License:        GPLv2
URL:            https://github.com/legoater/vfio-tests
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
Requires:       python3

%description
User-space test programs for the Linux VFIO and IOMMUFD subsystems.
Exercises PCI device open, DMA mapping, hot reset, hugepage fault handling,
and live migration through the VFIO kernel interfaces.

Tests require root and a PCI device bound to a VFIO driver (vfio-pci).

%prep
%autosetup

%build
%make_build

%install
install -d %{buildroot}%{_sharedstatedir}/%{name}

# Compiled test binaries
install -m 0755 vfio-correctness-tests %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 vfio-huge-guest-test %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 vfio-iommu-map-unmap %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 vfio-iommu-stress-test %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 vfio-noiommu-pci-device-open %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 vfio-pci-device-open %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 vfio-pci-device-open-igd %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 vfio-pci-device-open-sparse-mmap %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 vfio-pci-hot-reset %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 vfio-pci-device-dma-map %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 vfio-pci-huge-fault-race %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 iommufd-pci-device-open %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 iommufd-dma-map-unmap %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 iommufd-dmabuf %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 vfio-pci-device-migration %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 vfio-pci-device-migration-stress %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 vfio-pci-device-map-alignment %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 vfio-pci-bar-fault-timing %{buildroot}%{_sharedstatedir}/%{name}/

# Shell scripts
install -m 0755 run-test.sh %{buildroot}%{_sharedstatedir}/%{name}/
install -m 0755 iova-stress.sh %{buildroot}%{_sharedstatedir}/%{name}/

# Tools script
install -d %{buildroot}%{_sharedstatedir}/%{name}/tools/
install -m 0755 tools/vfio-check-alignment.py %{buildroot}%{_sharedstatedir}/%{name}/tools/

%files
%license COPYING
%doc README.md
%dir %{_sharedstatedir}/%{name}
%{_sharedstatedir}/%{name}/*

%changelog
* Thu Aug 27 2026 Rodolfo Vick <rovick@redhat.com> - 0.1.0-1
- Initial RPM package
