#!/bin/sh -eu

# This script only relies on access to GitHub and can be run from anywhere.
# Resulting packages will be placed in the current working directory.
# Environment variables:
# GIT_BRANCH - which branch to build
# BASE - which distribution to use for packaging

GIT_BRANCH=${GIT_BRANCH:='master'}
BASE=${BASE:='debian:stable'}

docker buildx build -t glb-builder-${BASE} --build-arg base=${BASE} \
       https://github.com/codership/glb.git\#${GIT_BRANCH}:packaging/deb/
mkdir ${BASE} # output dir for packages
docker run -v ${PWD}:/output --env GIT_BRANCH=${GIT_BRANCH} \
       --env BASE=${BASE} glb-builder-${BASE}
