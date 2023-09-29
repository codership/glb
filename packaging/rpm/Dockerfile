# Use a base image with a Linux distribution of your choice
# that supports RPM package building, e.g., CentOS or Fedora.

ARG base=rockylinux:8
FROM ${base}

# Install necessary packages to build RPM
RUN yum install -y epel-release && \
    yum install -y git rpm-build rpmdevtools && \
    yum install -y autoconf automake gcc libtool make && \
    yum clean all

# Set up RPM build environment
RUN rpmdev-setuptree

# Set up the build script to be executed on docker run
COPY entrypoint.sh /root
WORKDIR /root
ENTRYPOINT /root/entrypoint.sh
