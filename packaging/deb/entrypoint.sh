#!/bin/bash -eu

# Clone the GitHub repository using the specified branch
git clone --depth 1 --branch ${GIT_BRANCH:='master'} https://github.com/codership/glb /root/glb
cd glb

# Build the DEB package
dpkg-buildpackage -uc -us

# Verify if the DEB build was successful
if [ $? -eq 0 ]; then
  echo "DEB package built successfully."
else
  echo "Error: DEB package build failed."
  exit 1
fi

# Move the DEB package to the mounted volume
OUTPUT=/output/${BASE}/
mv /root/*.deb ${OUTPUT}
echo "DEB package moved to: ${OUTPUT}"
