# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

function install_buildenv() {
    dnf update -y --nogpgcheck fedora-gpg-keys
    dnf distro-sync -y
    dnf install -y \
        autoconf \
        automake \
        ca-certificates \
        ccache \
        gcc \
        gettext-devel \
        git \
        glibc-langpack-en \
        libcmpiutil-devel \
        libconfig-devel \
        libtool \
        libuuid-devel \
        libvirt-devel \
        libxml2-devel \
        libxslt-devel \
        make \
        pkgconfig \
        rpm-build \
        wget \
        xz
    rpm -qa | sort > /packages.txt
    mkdir -p /usr/libexec/ccache-wrappers
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/cc
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/gcc
}

export CCACHE_WRAPPERSDIR="/usr/libexec/ccache-wrappers"
export LANG="en_US.UTF-8"
export MAKE="/usr/bin/make"
