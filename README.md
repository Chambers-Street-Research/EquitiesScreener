# EquitiesScreener

A lightweight C++ equity screening engine that filters and ranks stocks based on fundamental valuation and profitability metrics.

## Features

- **CSV I/O** — load equity universes from CSV files and export screened results back to CSV
- **File-based settings** — describe your screens in an INI file (`screener.ini`) and run them all with one command
- **Multi-metric filtering** — screen equities by P/E, Forward P/E, P/B, P/S, EV/EBITDA, ROA, ROE, and ROIC
- **Chainable filter API** — compose screens fluently (e.g. `.filterPE(0, 30).filterROE(0.15)`)
- **Sorting** — sort results ascending or descending by any supported metric
- **Extensible universe** — add equities individually or in bulk via `addEquity` / `setUniverse`

## Project Structure

```
EquitiesScreener/
├── data/
│   ├── Equity.h        # Equity data model & Metric enum
│   └── Equity.cpp
├── engine/
│   ├── Engine.h        # Screening engine interface
│   └── Engine.cpp      # Filter, screen, sort logic
├── io/
│   ├── Csv.h           # CSV reader & writer API
│   └── Csv.cpp         # Parsing & formatting with C++23 ranges
├── config/
│   ├── Config.h        # Settings-file model & INI parser API
│   ├── Config.cpp      # INI parsing, metric names, output naming
│   └── Config_test.cpp # Parser unit tests (no framework)
├── main.cpp            # CLI — CSV in, filtered/sorted CSV out
├── WorkingData/        # Your working files (mounted into the Docker container)
│   ├── screener.ini    #   Base config - run the tool with no args to try it
│   └── sample.csv      #   7-ticker sample universe
├── compose.yaml        # Docker Compose: mounts WorkingData/ into the container
├── screener.ini.example  # Annotated settings-file template
└── CMakeLists.txt
```

## Build

Requires **CMake ≥ 3.20** and a **C++23**-capable compiler (GCC, Clang, or MSVC).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/EquitiesScreener
```

**Quick start:** after building, run it from inside `WorkingData/` — it
auto-loads `screener.ini`, screens `sample.csv`, and writes
`value_screened.csv` and `growth_screened.csv` next to them:

```bash
cd WorkingData
../build/EquitiesScreener
```

Or open the project directly in **CLion** — it will pick up `CMakeLists.txt` automatically.

## Tests

Minimal framework-free unit tests (plain asserts) for the settings-file parser:

```bash
cmake --build build --config Release
./build/Release/EquitiesScreenerConfigTests.exe
```

Exit code `0` means all assertions passed.

## Docker

The container is designed to be used like a CLI: everything it reads and
writes lives in the repo's `WorkingData/` folder, which Compose mounts into
the container at `/work`. Edit files on your host, re-run — **no rebuilds**
for data or config changes.

### Interactive shell (recommended)

```bash
docker compose up        # builds on first run, then opens a bash shell in /work
```

You're now inside the container, in `/work` — which **is** your host's
`WorkingData/` folder (mounted, so edits and outputs appear on both sides):

```bash
pwd                       # /work
ls                        # your WorkingData files
EquitiesScreener          # run the screens -> value_screened.csv & growth_screened.csv
```

The container's main process is `bash`, so it stays alive while you work.
Exit the shell when done (`exit`) — the container exits too, and `restart` is
explicitly off, so it never comes back on its own. **Take it down with:**

```bash
docker compose down
```

> `docker compose down` removes the container and its network. The container
> stops by itself when you exit the shell (auto-restart is off), but it is
> not removed — after `up` (or `up -d`), run `docker compose down` to take it
> down completely.

### One-shot run (no shell)

```bash
# via Compose (the entrypoint recognizes the binary name)
docker compose run --rm screener EquitiesScreener

# with extra flags
docker compose run --rm screener EquitiesScreener -p

# via plain docker (no Compose needed)
docker run --rm -v "$(pwd)/WorkingData:/work" equities-screener
```

### Hop into a shell with plain docker

```bash
docker run --rm -it -v "$(pwd)/WorkingData:/work" equities-screener bash
```

### Configuration via environment variables

| Env var | Effect |
|---------|--------|
| `SCREENER_CONFIG=<file.ini>` | Use this settings file instead of `screener.ini` (relative to `/work`) |
| `SCREENER_INPUT=<universe.csv>` | Override the universe CSV (relative to `/work`) |
| `SCREENER_PRETTY=1` | Also pretty-print results to the terminal |

### Smart defaults

- The image ships default `screener.ini` + `sample.csv` at
  `/opt/equities-screener/` — outside `/work`, so a mount can't hide them.
- On startup the entrypoint copies a missing file into `/work`; it never
  overwrites a file you mounted. An empty or partial mount "just works".
- Without any mount, `docker run equities-screener` still runs the sample
  screens — but outputs are written into the container's own filesystem and
  are lost when it exits. Mount `/work` to keep them.

### Windows

- **Git Bash / WSL:** `-v "$(pwd)/WorkingData:/work"` works.
- **PowerShell:** `-v "${PWD}/WorkingData:/work"` or a literal
  `-v C:\path\to\WorkingData:/work`.
- Avoid `/tmp/...`-style paths in `-v` — Git Bash mangles them; use a full
  Windows path instead. With Compose (`docker compose up`) none of this
  matters — the mount is defined in `compose.yaml`.

## CLI Usage

Two modes: the original **flag-based** mode, and **settings-file** mode (see
[Settings File](#settings-file-screenerini) below).

```bash
# ── Settings-file mode ─────────────────────────────────────────────────────
EquitiesScreener                       # auto-load ./screener.ini, run all screens
EquitiesScreener --config my.ini       # run the screens in my.ini

# ── Flag-based mode (unchanged) ────────────────────────────────────────────
# Pipe screened stocks to stdout (Unix filter style)
EquitiesScreener stocks.csv -f PE:0:30 -f ROE:0.15 -s ROE > value.csv

# Write to file
EquitiesScreener stocks.csv -f ROIC:0.25: -o high_roic.csv

# Pretty-print to terminal
EquitiesScreener stocks.csv -f PE:0:30 -f ROE:0.15 -p

# Show help
EquitiesScreener -h
```

### Flags

| Flag | Meaning |
|------|---------|
| `-c, --config <FILE>` | Use `<FILE>` as the settings file |
| `-i, --input <FILE>` | Override the universe CSV declared in the settings file |
| `-f, --filter <METRIC:MIN:MAX>` | Add a filter (repeatable) |
| `-s, --sort <METRIC[:ASC\|DESC]>` | Sort results (default: descending) |
| `-o, --output <FILE>` | Write CSV to a file (default: stdout) |
| `-p, --pretty` | Pretty-print results to the terminal |
| `-h, --help` | Show help |

> If `screener.ini` does not exist and no `--config`/flags are given, the program
> prints usage and exits with code `2`.


## Settings File (`screener.ini`)

The screener can be configured from a plain-text INI file instead of CLI flags.
You declare one or more **screens** — each with its own filters, sort order, and
output file — and the program runs them all automatically. A fully annotated
template lives in **[`screener.ini.example`](screener.ini.example)**: copy it to
`screener.ini` and edit.

### Running in settings mode

| Command | What it does |
|---------|--------------|
| `EquitiesScreener` | Auto-loads `./screener.ini` and runs every screen in it |
| `EquitiesScreener --config my.ini` | Runs the screens in `my.ini` |
| `EquitiesScreener --config my.ini stocks.csv` | Same, but reads `stocks.csv` instead of the file's `input` |
| `EquitiesScreener --config my.ini -f PE:0:10` | Same, plus an extra filter added to *every* screen |

### File conventions

- **Comments** — lines starting with `#` or `;` (whole-line only).
- **Case-insensitive** — keys, metric names, and sort orders (`PE` = `pe`, `Asc` = `asc`).
- **Blank lines** are ignored; values are trimmed of surrounding whitespace.
- **Paths** are relative to the current working directory.
- **Duplicates** — a repeated scalar key (two `input =` lines, or two
  `[screen:value]` sections) is an error. Repeated `filter =` lines are
  intentional and additive.
- **Errors fail fast** with the offending line number.

### The `input` key

A global key, placed before any `[screen:...]` section:

```ini
input = stocks.csv
```

The 13-column universe CSV (same layout as `sample.csv`). It is read **once**
and shared by every screen. Overridable from the CLI with a positional argument
or `--input`.

### Screen sections — `[screen:NAME]`

Each section is one independent screen. Every key is optional except the
section header itself (a screen with no keys is a valid pass-through "export"
screen).

| Key | Meaning |
|-----|---------|
| `filter = METRIC:MIN:MAX` | One filter rule; repeat for more (AND-combined) |
| `sort = METRIC[:ASC\|DESC]` | Result ordering; default descending |
| `output = <file.csv>` | Output path; default `{name}_screened.csv` |

### `filter` in depth

Syntax: `filter = METRIC:MIN:MAX` — a stock passes when `MIN <= value <= MAX`.

- **Metrics:** `SPOTPRICE`, `PE`, `FORWARDPE`, `PB`, `PS`, `EVEBITDA`, `ROA`,
  `ROE`, `ROIC` (case-insensitive).
- **ROA / ROE / ROIC use decimals**, exactly like the CSV (`0.15` = 15%).
- **MIN and MAX are inclusive**; leave either empty for an open bound.

| Example | Meaning |
|---------|---------|
| `filter = PE:0:30` | P/E between 0 and 30 |
| `filter = ROE:0.15:` | ROE at least 15% (no upper bound) |
| `filter = EVEBITDA::20` | EV/EBITDA at most 20 (no lower bound) |
| `filter = SPOTPRICE:50:500` | Spot price between $50 and $500 |

Multiple `filter =` lines **AND together** — a stock must satisfy all of them.

### `sort` in depth

```ini
sort = METRIC[:ASC|DESC]
```

The order suffix is optional and defaults to **descending** (largest first).
`sort = PE:asc` sorts ascending.

### `output` in depth

Write the screened CSV to this path (relative to the working directory). If
omitted, the program auto-names it `{screen-name}_screened.csv` —
`[screen:value]` → `value_screened.csv`.

### Missing data

If a stock is missing the metric a filter asks about (blank cell in the CSV),
it is **excluded from that screen** and a warning is printed to stderr. A
missing value never silently counts as zero.

### CLI overrides (precedence)

CLI flags extend/override the settings file:

| Flag | Effect on settings mode |
|------|--------------------------|
| `-f, --filter METRIC:MIN:MAX` | Added to **every** screen in the file |
| `-s, --sort METRIC[:ASC]` | Overrides **every** screen's sort |
| `-o, --output FILE` | Overrides output — only valid when the file has **one** screen |
| `-p, --pretty` | Also pretty-prints results to the terminal |
| `-i, --input FILE` | Overrides the file's `input` |

### Errors & exit codes

- `0` success · `1` config/runtime error · `2` usage error.
- Settings-file problems (unknown key, bad number, duplicate screen) stop the
  run immediately with the line number, e.g. `screener.ini:12: ...`.

### Example

```ini
input = stocks.csv

[screen:value]
filter = PE:0:30
filter = ROE:0.15:
filter = EVEBITDA::20
sort = ROE:desc
output = value_screened.csv

[screen:growth]
filter = ROIC:0.25:
sort = ROIC:asc
```

Running `EquitiesScreener` writes two files:

- **`value_screened.csv`** — every stock with `0 ≤ PE ≤ 30`, `ROE ≥ 15%`,
  `EV/EBITDA ≤ 20`, sorted by ROE descending.
- **`growth_screened.csv`** — every stock with `ROIC ≥ 25%`, sorted by ROIC
  ascending.

Both contain the **full 13-column data** for each matched stock.

## C++ Library API

```cpp
#include "data/Equity.h"
#include "engine/Engine.h"
#include "io/Csv.h"

// Read CSV into the engine
auto csv = IO::readCsv("stocks.csv");
if (!csv) { /* handle error */ }

Engine::Engine screener(std::move(csv->equities));
screener.filterPE(0.0f, 30.0f)
        .filterROE(0.15f)
        .filterEVEBITDA(0.0f, 20.0f);

auto results = screener.runScreenAndSort(Data::Metric::ROE, Engine::SortOrder::Descending);

// Write results back to CSV
IO::writeCsv("screened.csv", results);
screener.printResults(results);
```

Settings-file mode is equally scriptable — parse a `screener.ini` into screen
configs and drive the same engine:

```cpp
#include "config/Config.h"

// Parse an INI settings file (error string includes file:line on failure)
auto config = Config::parseSettingsFile("screener.ini");
if (!config) { /* handle config.error() */ }

for (const auto& screen : config->screens) {
    // screen.filters, screen.sort, screen.outputFile / Config::defaultOutputName(name)
}
```

## Supported Metrics

| Metric | Method |
|--------|--------|
| Spot Price | `filterSpotPrice(min, max)` |
| P/E Ratio | `filterPE(min, max)` |
| Forward P/E | `filterForwardPE(min, max)` |
| P/B Ratio | `filterPB(min, max)` |
| P/S Ratio | `filterPS(min, max)` |
| EV/EBITDA | `filterEVEBITDA(min, max)` |
| ROA | `filterROA(min, max)` |
| ROE | `filterROE(min, max)` |
| ROIC | `filterROIC(min, max)` |

## License

MIT
