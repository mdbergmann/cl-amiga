# Debian Bookworm (arm64) verification image for clamiga.
#
# Deliberately minimal: clamiga's host build needs gcc/make/libc and
# libffi (platform_posix.c).  libsqlite3 is for the clog dependency of
# chipi-ui (loaded via CFFI at run time), tzdata for local-time, iproute2
# so the entry script can alias the LAN address eta-hab binds to.
FROM debian:bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        libffi-dev \
        ca-certificates \
        tzdata \
        iproute2 \
        libsqlite3-0 \
        curl \
    && rm -rf /var/lib/apt/lists/*

# Stub pigpio libraries: cl-pigpio LOAD-FOREIGN-LIBRARYs libpigpio.so at
# load time (real GPIO only exists on the Pi).  An empty .so satisfies the
# dlopen; the gpio* symbols resolve lazily per call, and eta-hab wraps its
# hardware init in IGNORE-ERRORS.
RUN echo 'int __clamiga_pigpio_stub;' > /tmp/stub.c \
    && gcc -shared -o /usr/local/lib/libpigpio.so /tmp/stub.c \
    && gcc -shared -o /usr/local/lib/libpigpiod_if.so /tmp/stub.c \
    && gcc -shared -o /usr/local/lib/libpigpiod_if2.so /tmp/stub.c \
    && ldconfig && rm /tmp/stub.c

WORKDIR /work
