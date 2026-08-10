#!/bin/sh
#
# Translates container environment variables into EquitiesScreener CLI flags,
# seeds the working directory with image defaults, and supports interactive
# shells.
#
# The binary itself is env-agnostic (every option is a flag, every path is
# relative to the working directory), so this shim is what makes the image
# configurable the way a container is expected to be. Env-derived flags are
# *prepended*, which means anything passed to `docker run` still comes later
# on the command line and wins.
#
# WORKING DIRECTORY / DATA
# ------------------------
# Everything the screener reads or writes is relative to the working
# directory (/work). Bind-mount your data directory there to use your own
# universe CSVs and screens without rebuilding the image:
#
#     docker run --rm -v "$(pwd)/WorkingData:/work" equities-screener
#
# Defaults shipped in the image (/opt/equities-screener) are copied into
# /work ONLY when a file is missing, so an empty or partial mount just works
# and user-provided files are never overwritten.
#
# INTERACTIVE SHELLS
# ------------------
# `docker compose up` / `docker compose run --rm screener bash` drops you into
# a shell instead of running the screener (the container stays alive). Run
# `EquitiesScreener` from inside it.
#
set -eu

# Seed defaults (never overwrite user-provided files). Seeding is
# best-effort: a read-only or foreign-owned mount must not prevent the
# tool from running.
if [ ! -e /work/screener.ini ]; then
    if ! cp /opt/equities-screener/screener.ini /work/screener.ini 2>/dev/null; then
        echo "warning: could not seed /work/screener.ini (read-only mount?)" >&2
    fi
fi
if [ ! -e /work/sample.csv ]; then
    if ! cp /opt/equities-screener/sample.csv /work/sample.csv 2>/dev/null; then
        echo "warning: could not seed /work/sample.csv (read-only mount?)" >&2
    fi
fi

# Interactive shells: exec straight through so `... screener bash` (or
# `/bin/bash`) opens a shell instead of being passed to the screener as an
# input file. Match on the basename so absolute paths work too.
case "${1##*/}" in
    bash|sh|ash|dash|zsh|ksh) exec "$@" ;;
esac

# `docker compose run --rm screener EquitiesScreener [flags]` is a natural
# one-shot invocation; drop the binary name and run with the remaining args.
if [ "${1:-}" = "EquitiesScreener" ]; then
    shift
fi

if [ -n "${SCREENER_CONFIG:-}" ]; then
    set -- --config "$SCREENER_CONFIG" "$@"
fi

if [ -n "${SCREENER_INPUT:-}" ]; then
    set -- --input "$SCREENER_INPUT" "$@"
fi

if [ -n "${SCREENER_PRETTY:-}" ]; then
    set -- --pretty "$@"
fi

exec EquitiesScreener "$@"
