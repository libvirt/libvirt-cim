# THIS FILE WAS AUTO-GENERATED
#
#  $ lcitool dockerfile centos-7 libvirt+minimal,libvirt+dist,libvirt-cim
#
# https://gitlab.com/libvirt/libvirt-ci/-/commit/d527e0c012f476c293f3bc801b7da08bc85f98ef
FROM docker.io/library/centos:7

RUN yum update -y && \
    echo 'skip_missing_names_on_install=0' >> /etc/yum.conf && \
    yum install -y epel-release && \
    yum install -y \
        autoconf \
        automake \
        ca-certificates \
        ccache \
        gcc \
        gettext \
        gettext-devel \
        git \
        glib2-devel \
        glibc-common \
        glibc-devel \
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
        ninja-build \
        perl \
        pkgconfig \
        python3 \
        python3-pip \
        python3-setuptools \
        python3-wheel \
        python36-docutils \
        rpm-build \
        wget && \
    yum autoremove -y && \
    yum clean all -y && \
    rpm -qa | sort > /packages.txt && \
    mkdir -p /usr/libexec/ccache-wrappers && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/cc && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/$(basename /usr/bin/gcc)

RUN pip3 install \
         meson==0.54.0

ENV LANG "en_US.UTF-8"
ENV MAKE "/usr/bin/make"
ENV NINJA "/usr/bin/ninja-build"
ENV PYTHON "/usr/bin/python3"
ENV CCACHE_WRAPPERSDIR "/usr/libexec/ccache-wrappers"
