FROM fedora:43@sha256:762d73ba1c455232b0272c5d445a34f36c4b9f421cbc05ce8102552325b6a222 AS builder

RUN dnf install -y --setopt=install_weak_deps=False \
    # Compiler & build
    gcc-c++ \
    cmake \
    make \
    ninja-build \
    # Google Test
    gtest-devel \
    gmock-devel \
    # Debug & analysis
    gdb \
    valgrind \
    strace \
    file \
    # Quality of life
    git \
    openssl-devel \
    # PostgreSQL client (to connect to TimescaleDB)
    libpq-devel \
    libpqxx-devel \
    postgresql \
    && dnf clean all


WORKDIR /app

CMD ["bash"]