#!/bin/bash -eu

# Clone the GitHub repository using the specified branch
git clone --depth 1 --branch ${GIT_BRANCH:='master'} https://github.com/codership/glb /root/glb
cd glb

# Check if the RPM spec file exists
SPEC_FILE="glbd.spec"
if [ ! -f "$SPEC_FILE" ]; then
  echo "Error: $SPEC_FILE not found in the current directory."
  exit 1
fi

# Set the RPM build directory
RPMBUILD_DIR="${HOME}/rpmbuild"

# Find package version
GLBD_VERSION=$(grep Version: "$SPEC_FILE" | tr -s ' ' | cut -d ' ' -f 2)
PREFIX="glbd-${GLBD_VERSION:='x.y.z'}"
git archive --format=tar.gz -o /root/rpmbuild/SOURCES/${PREFIX}.tar.gz --prefix=${PREFIX}/ ${GIT_BRANCH}

# Build the RPM package
rpmbuild -ba "$SPEC_FILE"

# Verify if the RPM build was successful
if [ $? -eq 0 ]; then
  echo "RPM package built successfully."
else
  echo "Error: RPM package build failed."
  exit 1
fi

# Move the RPM package to the mounted volume
OUTPUT="/output/${BASE}"
mv "${RPMBUILD_DIR}/RPMS/x86_64/"*.rpm "${OUTPUT}"
echo "RPM package moved to: ${OUTPUT}"
