FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        binutils \
        build-essential \
        binutils-i686-linux-gnu \
        gcc-i686-linux-gnu \
        make \
        python3 \
        qemu-system-x86 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY . /workspace

ENV AS=/usr/bin/i686-linux-gnu-as
ENV CC=/usr/bin/i686-linux-gnu-gcc
ENV CXX=/usr/bin/g++
ENV LD=/usr/bin/i686-linux-gnu-ld
ENV AR=/usr/bin/i686-linux-gnu-ar
ENV MAKE=/usr/bin/make
ENV QEMU_CMD=/usr/bin/qemu-system-i386
ENV SODEX_HOST_BIND_ADDR=0.0.0.0
ENV SODEX_HOST_HTTP_PORT=18080
ENV SODEX_HOST_ADMIN_PORT=10023

EXPOSE 18080 10023

ENTRYPOINT ["./docker/server-runtime/entrypoint.sh"]
