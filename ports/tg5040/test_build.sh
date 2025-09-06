#!/bin/bash
# Test script to verify MuPDF 1.26.7 build works correctly

set -e

echo "Testing MuPDF 1.26.7 build in Docker container..."

# Build the Docker image with latest MuPDF
echo "Building Docker image with MuPDF 1.26.7..."
cd /root/workspace/ports/tg5040
docker build -t tg5040-test .

# Test the build inside the container
echo "Testing build inside container..."
docker run --rm -v "$(pwd)/../..:/root/workspace" tg5040-test /bin/bash -c "
set -e
cd /root/workspace/ports/tg5040

# Check if MuPDF was installed correctly
echo 'Checking MuPDF installation...'
ls -la /usr/local/lib/libmupdf* || echo 'MuPDF libs not found in /usr/local/lib'
ls -la /usr/local/include/mupdf/ || echo 'MuPDF headers not found in /usr/local/include'

# Try to build the project
echo 'Building project...'
make -f Makefile clean
make -f Makefile

# Check if binary was created
echo 'Checking binary...'
ls -la bin/sdl_reader_cli

# Check dynamic dependencies
echo 'Checking dynamic dependencies...'
ldd bin/sdl_reader_cli | grep -i mupdf || echo 'MuPDF not found in ldd output'

echo 'Build test completed successfully!'
"

echo "Test completed!"
