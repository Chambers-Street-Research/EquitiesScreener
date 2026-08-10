# ============================================================
#  EquitiesScreener - containerized build
# ============================================================

# ------------------------------------------------------------
#  Stage 1: builder
# ------------------------------------------------------------
FROM gcc:16 AS builder

# The gcc image ships the toolchain but no build generator.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        cmake \
        ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copied as separate layers so editing a .cpp does not re-run CMake configure
# unless CMakeLists.txt itself changed.
COPY CMakeLists.txt ./
COPY config/ config/
COPY data/ data/
COPY engine/ engine/
COPY io/ io/
COPY main.cpp ./

RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
    && cmake --build build

# Smoke-test the freshly linked binary so a broken build fails here rather
# than at `docker run`. --help exits 0 and touches no input files.
RUN ./build/EquitiesScreener --help > /dev/null

# ------------------------------------------------------------
#  Stage 2: runtime
# ------------------------------------------------------------
FROM debian:trixie-slim AS runtime

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
