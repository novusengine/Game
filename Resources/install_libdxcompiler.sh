#!/bin/bash

# This script installs the dxcompiler library on the system. This script needs to
# be ran as sudo or with the appropriate permissions to install shared libraries.
#
# This has only been tested on Ubuntu 24.04, make sure the script is appropriate
# for your distro, or do the steps below manually in accordance with your distro.

# Determine the absolute path of this script file
SCRIPT_DIR="$(dirname "$(realpath "$0")")"

# Define source file and destination path
SOURCE_FILE="$SCRIPT_DIR/../Submodules/Engine/Dependencies/dxcompiler/lib/linux/libdxcompiler.so"
DEST_PATH="/usr/lib"

# Check if the dxcompiler library exists
if [ ! -f "$SOURCE_FILE" ]; then
    echo "File does not exist: $SOURCE_FILE"
    exit 1
fi

# Copy the dxcompiler library to the destination directory
sudo cp "$SOURCE_FILE" "$DEST_PATH"

# Verify the copy operation
if [ $? -ne 0 ]; then
    echo "Failed to copy the dxcompiler library to $DEST_PATH"
    exit 1
fi

# Update the shared library cache
sudo ldconfig

# Confirm successful update
if [ $? -eq 0 ]; then
    echo "The dxcompiler library was successfully installed."
else
    echo "Failed to install the dxcompiler library."
    exit 1
fi
