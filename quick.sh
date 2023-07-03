set -e

ROOT=`realpath .`
COMMIT=`git rev-parse --short HEAD`
INSTALL=~/glibc-2.28-zzz-$COMMIT

make -j`nproc --all`
make -j`nproc --all` install

module load patchelf-0.14.5
patchelf --remove-rpath $INSTALL/lib/ld-2.28.so
patchelf --remove-rpath elf/ld.so

echo "done"
