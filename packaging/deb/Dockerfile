# Use a base image with a Linux distribution of your choice
# that supports RPM package building, e.g., CentOS or Fedora.

ARG base=debian:stable
FROM ${base}

# Install necessary packages to build DEB
RUN apt-get update && \
    apt-get install -y git dpkg-dev && \
    apt-get install -y autoconf automake debhelper libtool && \
    rm -rf /var/lib/apt/lists/*

# Set up the build script to be executed on docker run
COPY entrypoint.sh /root
WORKDIR /root
ENTRYPOINT /root/entrypoint.sh
