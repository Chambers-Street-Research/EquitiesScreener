#!/bin/sh
#
# Translates container environment variables into EquitiesScreener CLI flags.
#
# The binary itself is env-agnostic (every option is a flag, every path is
# relative to the working directory), so this shim is what makes the image
# configurable the way a container is expected to be. Env-derived flags are
# *prepended*, which means anything passed to `docker run` still comes later
# on the command line and wins.
#
set -eu

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
