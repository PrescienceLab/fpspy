make clean
(cd kernel/x64/fpvm-kmod; make clean)
rm -f `find . -name "*~"`
tar cvfz fpspy-distro.tgz \
    bin \
    Dockerfile.ubuntu \
    ENV.arm64 \
    ENV.riscv64 \
    ENV.x64 \
    extra \
    include \
    kernel \
    lib \
    LICENSE \
    Makefile \
    README \
    scripts \
    src \
    test
