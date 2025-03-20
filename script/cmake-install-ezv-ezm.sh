#!/usr/bin/env bash

INSTALL_DIR=/tmp/easypap

rm -rf build

# Configure (CONFIGURE_EASYPAP=OFF avoid using unnecessary dependencies on OpenSSL3, MPI, etc.
CC=${CC:-gcc} cmake -S . -B build -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} \
                                  -DCMAKE_BUILD_TYPE=Release \
                                  -DCONFIGURE_EASYPAP=OFF || exit $?

# Build ezv, ezm and easyview
cmake --build build --target ezv --parallel || exit $?
#cmake --build build --target ezm --parallel || exit $?

# install
#rm -rf ${INSTALL_DIR}
cmake --install build --component ezv || exit $?
#cmake --install build --component ezm || exit $?
