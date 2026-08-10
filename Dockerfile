# ============================================================
#  EquitiesScreener - containerized build
# ============================================================

# ------------------------------------------------------------
#  Stage 1: builder
# ------------------------------------------------------------
FROM gcc:16@sha256:a612916cfba059f0b531f7aebddca462bf23a6d7a9c6681ca3b110f7721d6d05 AS builder # pinned 2026-08-10

# The gcc image ships the toolchain but no build generator or vcpkg deps.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        cmake \
        ninja-build \
        git \
        curl \
        zip \
        unzip \
        tar \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# vcpkg in manifest mode (dependencies come from vcpkg.json). Cloned BEFORE
# the source COPY so dependency builds are cached across source edits. Pinned
# to the same commit as the vcpkg.json builtin-baseline for reproducibility;
# override with --build-arg VCPKG_BASELINE=<sha>.
ENV VCPKG_ROOT=/opt/vcpkg
ARG VCPKG_BASELINE=f29188ab71f34a184e1868147bea6e519d9d74e4
RUN git clone --depth 1 https://github.com/microsoft/vcpkg ${VCPKG_ROOT} \
    && git -C ${VCPKG_ROOT} fetch --depth 1 origin ${VCPKG_BASELINE} \
    && git -C ${VCPKG_ROOT} checkout ${VCPKG_BASELINE} \
    && ${VCPKG_ROOT}/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /src

# Copied as separate layers so editing a .cpp does not re-run CMake configure
# unless CMakeLists.txt itself changed.
COPY CMakeLists.txt ./
COPY vcpkg.json ./
COPY config/ config/
COPY data/ data/
COPY engine/ engine/
COPY io/ io/
COPY tests/ tests/
COPY main.cpp ./

RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
    && cmake --build build

# Smoke-test the freshly linked binary so a broken build fails here rather
# than at `docker run`. --help exits 0 and touches no input files.
RUN ./build/EquitiesScreener --help > /dev/null

# ------------------------------------------------------------
#  Stage 2: test - unit tests (GoogleTest via vcpkg)
# ------------------------------------------------------------
FROM builder AS test

# Runs the gtest suites as their own stage so CI can gate on them
# independently (`docker build --target test .`). A full image build runs this
# stage too, so the gate also protects the shipped image. gtest must be
# STATICALLY embedded in the test binary ("fat binary") - no dynamic gtest
# libraries allowed.
RUN ./build/equities_tests \
    && ! ldd ./build/equities_tests | grep -i gtest \
    && nm -C ./build/equities_tests | grep -q 'testing::Test' \
    && echo 'OK: gtest statically linked into equities_tests'

# ------------------------------------------------------------
#  Stage 3: runtime
# ------------------------------------------------------------
FROM debian:trixie-slim@sha256:3a39a0592364683e6bab97937b72cad5a8fa6dcbbee90edb3bb48c7f8e94f258 AS runtime # pinned 2026-08-10

# Unprivileged by default;
# user, so CSVs written into a bind-mounted directory stay editable on the host.
RUN useradd --create-home --uid 1000 screener \
    && mkdir -p /work \
    && chown screener:screener /work

COPY --from=builder /src/build/EquitiesScreener /usr/local/bin/EquitiesScreener
COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod 0755 /usr/local/bin/entrypoint.sh

# Baked-in defaults so `docker run <image>` does something useful with no
# volumes attached. They live OUTSIDE /work so a bind mount over /work (the
# container's data directory) never hides them; the entrypoint seeds /work
# from here only when files are missing.
COPY --chown=screener:screener WorkingData/screener.ini WorkingData/sample.csv /opt/equities-screener/

# Every path the screener reads or writes is relative to the working directory,
# so /work is the single mount point that matters.
WORKDIR /work
USER screener

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD []
