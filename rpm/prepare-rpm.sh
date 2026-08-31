#! /bin/bash

# Release version, change from new releases
VERSION="0.1.0"
NAME="vfio-tests"
TARBALL="${NAME}-${VERSION}.tar.gz"

# Copy all necessary files to rpmbuild folder
mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

echo -e ":: \e[1;32mCopy source tarball to rpmbuild\e[0m"
git archive --format=tar.gz --prefix="${NAME}-${VERSION}/" HEAD -o ~/rpmbuild/SOURCES/${TARBALL}

echo -e ":: \e[1;32mCopy spec file to rpmbuild\e[0m"
cp rpm/vfio-tests.spec ~/rpmbuild/SPECS/

echo -e "\e[36mNow build the rpm with: \e[1;37mrpmbuild -ba ~/rpmbuild/SPECS/vfio-tests.spec\e[0m"
