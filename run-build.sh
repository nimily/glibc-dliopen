set -e

GLIBC_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
BUILD_DIR=$GLIBC_DIR/build
echo "Build dir: $BUILD_DIR"

cd $GLIBC_DIR
COMMIT=`git rev-parse --short HEAD`
INSTALL_DIR=~/glibc-2.28-zzz-$COMMIT
echo "Install dir: $INSTALL_DIR"

if [ ! -d $BUILD_DIR ]; then
    mkdir -p $BUILD_DIR
    cd $BUILD_DIR
    ../configure --prefix $INSTALL_DIR --disable-werror
else
    cd $BUILD_DIR
fi


make -j`nproc --all`
make -j`nproc --all` install

patchelf --remove-rpath $INSTALL_DIR/lib/ld-2.28.so
patchelf --remove-rpath elf/ld.so

ln -fs /usr/share/zoneinfo $INSTALL_DIR/share/zoneinfo
ln -fs /etc/localtime $INSTALL_DIR/etc/localtime
ln -fs ~/miniconda3/envs/zzz_conda3/lib/libstdc++.so* $INSTALL_DIR/lib/

make localedata/install-locales
echo "Installation completed [installdir=$INSTALL_DIR]"
