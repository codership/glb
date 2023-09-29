#!/bin/sh -eu

# Resulting packages will be placed in the current working directory.
# Environment variables:
# GIT_BRANCH - which branch to build

GIT_BRANCH=${GIT_BRANCH:='master'}

DIRNAME=$(dirname $0)

BASE='centos:7'      ${DIRNAME}/rpm/build.sh
BASE='rockylinux:8'  ${DIRNAME}/rpm/build.sh

BASE='debian:stable' ${DIRNAME}/deb/build.sh
BASE='ubuntu:22.04'  ${DIRNAME}/deb/build.sh
