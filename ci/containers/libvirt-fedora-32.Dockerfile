FROM registry.fedoraproject.org/fedora:32

RUN dnf update -y && \
    dnf install -y \
        autoconf \
        automake \
        bash \
        bash-completion \
        ca-certificates \
        ccache \
        gcc \
        gettext \
        gettext-devel \
        git \
        glibc-devel \
        glibc-langpack-en \
        libcmpiutil-devel \
        libconfig-devel \
        libtool \
        libuuid-devel \
        libvirt-devel \
        libxml2-devel \
        libxslt-devel \
        make \
        patch \
        perl \
        perl-App-cpanminus \
        pkgconfig \
        python3 \
        python3-pip \
        python3-setuptools \
        python3-wheel \
        rpm-build \
        wget && \
    dnf autoremove -y && \
    dnf clean all -y && \
    mkdir -p /usr/libexec/ccache-wrappers && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/cc && \
    ln -s /usr/bin/ccache /usr/libexec/ccache-wrappers/$(basename /usr/bin/gcc)

ENV LANG "en_US.UTF-8"

ENV MAKE "/usr/bin/make"
ENV NINJA "/usr/bin/ninja"
ENV PYTHON "/usr/bin/python3"

ENV CCACHE_WRAPPERSDIR "/usr/libexec/ccache-wrappers"
