# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool manifest ci/manifest.yml
#
# https://gitlab.com/libvirt/libvirt-ci

function install_buildenv() {
    dnf update -y
    dnf install -y \
        autoconf \
        automake \
        ca-certificates \
        ccache \
        cpp \
        gcc \
        gettext \
        gettext-devel \
        git \
        glib2-devel \
        glibc-devel \
        glibc-langpack-en \
        gnutls-devel \
        libcmpiutil-devel \
        libconfig-devel \
        libnl3-devel \
        libtirpc-devel \
        libtool \
        libuuid-devel \
        libvirt-devel \
        libxml2 \
        libxml2-devel \
        libxslt \
        libxslt-devel \
        make \
        meson \
        ninja-build \
        perl-base \
        pkgconfig \
        python3 \
        python3-docutils \
        rpcgen \
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
export NINJA="/usr/bin/ninja"
export PYTHON="/usr/bin/python3"
