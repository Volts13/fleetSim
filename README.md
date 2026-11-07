# Fleet Simulation Engine

A C++17 discrete-time fleet simulation driven by newline-delimited JSON on standard input. The project implements vessel movement, dock and cargo operations, and historical committed-state queries.

## Requirements

- A C++17 compiler, such as GCC, Clang, or MSVC.
- Python 3 for the integration and protocol tests.
- The included `json.hpp` header. No separate JSON library installation is required.

Run all commands below from the project root: the folder containing `main.cpp`, `engine.cpp`, and `engine.hpp`.

## Files

| File or directory | Purpose |
| --- | --- |
| `main.cpp` | Reads JSON commands, dispatches them, and writes responses. |
| `engine.cpp`, `engine.hpp` | Simulation implementation and declarations. |
| `json.hpp` | Included nlohmann JSON header. |
| `tests/` | C++ unit tests and Python program-level tests. |
| `Saronic Design Document.md` | Design decisions and tradeoffs. |

Source files must be named `engine.cpp` and `engine.hpp`. Keep all test headers beside `tests/unit_tests.cpp`. The integration script should be named `tests/test_follow_on1.py` (without a download suffix such as `(1)`).

## Build and run: Linux / WSL

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic main.cpp engine.cpp -o fleet
./fleet
```

Enter one complete JSON command per line. The program writes one JSON response per command and continues until end-of-file. Press Ctrl+D on an empty line to finish an interactive session.

To read commands from a file you have created:

```bash
./fleet < commands.jsonl
```

## Build and run: Windows / MSVC

Open an **x64 Native Tools Command Prompt for Visual Studio**, navigate to the project root, and run:

```bat
cl /nologo /std:c++17 /EHsc /W4 /utf-8 main.cpp engine.cpp /Fe:fleet.exe
fleet.exe
```

Press Ctrl+Z followed by Enter to finish an interactive session in this command prompt. To use an input file:

```bat
fleet.exe < commands.jsonl
```

## Input and output

Each input line contains a `command` string and a `parameters` object. Use an empty object for commands without parameters.

For example, enter these four lines in order:

```json
{"command":"init","parameters":{"travel_times":[[0,4,7],[4,0,3],[7,3,0]],"dock_throughput":[2,4,1],"vessels":[{"id":"v1","location":0},{"id":"v2","location":2}]}}
{"command":"set_destination","parameters":{"vessel_id":"v1","destination":2}}
{"command":"tick","parameters":{}}
{"command":"get_state_at","parameters":{"tick":0}}
```

The final response reports the initial committed state at tick 0, where v1 is idle. The live simulation remains at tick 1 with v1 in transit.

Successful responses have `status: "ok"` and a world-state `payload`. Errors have `status: "error"` and a `payload.message`. Successful world states include vessel and dock cargo arrays, even when empty.

| Command | Parameters |
| --- | --- |
| `init` | Required: `travel_times`, `dock_throughput`, `vessels`. Optional: `cargo_manifest`. |
| `tick` | Optional: `cargo_manifest`. |
| `set_destination` | `vessel_id`, `destination`. |
| `dock`, `undock` | `vessel_id`. |
| `load`, `unload` | `vessel_id`, `destination`. |
| `swap` | `vessel_id`, `load_destination`, `unload_destination`. |
| `get_state` | Empty object; returns the current planned state. |
| `get_state_at` | `tick`; returns the committed snapshot for that tick. |

`init` must be the first command and can succeed only once. A manifest entry contains `location`, `destination`, and a positive `quantity`. Historical queries accept integer ticks from 0 through the current tick.

## Run the C++ unit tests

The unit-test executable has its own `main()`, so compile it with `engine.cpp` using the command below.

Linux / WSL:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic -DFLEET_ENGINE_TESTS -I. tests/unit_tests.cpp engine.cpp -o unit_tests
./unit_tests
```

Windows, from the Visual Studio command prompt:

```bat
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /DFLEET_ENGINE_TESTS /I. tests\unit_tests.cpp engine.cpp /Fe:unit_tests.exe
unit_tests.exe
```

`FLEET_ENGINE_TESTS` enables the test accessor used to inspect state and exercise numeric boundaries. Apply it to both the test source and `engine.cpp`, as these commands do.

Expected summary:

```text
241 passed, 0 failed
```

To run only historical-query tests or list available cases:

```bash
./unit_tests get_state_at.
./unit_tests --list
```

On Windows, use `unit_tests.exe` instead of `./unit_tests`. The optional argument filters by test-name prefix. Any failed test or a filter matching no tests produces a nonzero exit code.

## Run the integration and protocol tests

Build the normal `fleet` executable first, then run:

```bash
python3 tests/test_follow_on1.py ./fleet
python3 tests/test_protocol.py ./fleet
```

On Windows:

```bat
python tests\test_follow_on1.py .\fleet.exe
python tests\test_protocol.py .\fleet.exe
```

Use `py` instead of `python` if your Windows Python installation provides that launcher.

- **Integration test:** Replays the Follow-on 1 example, including ticks 5-10 omitted in the PDF. It then queries ticks 1 and 13, rejects a query for tick 99, and verifies that the live state is unchanged. Expected result: `25/25 responses passed; 0 failed.`
- **Protocol tests:** Exercise the running program's JSON input/output, cargo handling, shared dock throughput, command ordering, malformed requests, and rejected-command state preservation. Expected result: `267 checks passed`, followed by a coverage description. Run this script without Python's `-O` option because it uses assertions.

The integration test checks complete responses and one output line per command, and has a 10-second process timeout. Both Python scripts return a nonzero exit code on failure.

## Test file guide

| File | Coverage or role |
| --- | --- |
| `tests/unit_tests.cpp` | Registers tests and reports results. |
| `tests/test_support.hpp` | Shared fixtures, JSON comparisons, and engine-state checks. |
| `tests/test_init.hpp` | Initialization and parameter validation. |
| `tests/test_movement.hpp` | Ticks, destinations, arrivals, and planned-state reporting. |
| `tests/test_docking.hpp` | Docking, undocking, and state restrictions. |
| `tests/test_cargo.hpp` | Loading, unloading, swapping, delivery, and throughput. |
| `tests/test_get_state_at.hpp` | Committed snapshots, preserved history, and invalid queries. |
| `tests/test_follow_on1.py` | Full example through stdin/stdout, followed by historical queries. |
| `tests/test_protocol.py` | Additional program-level regression checks. |
| `tests/catalog.json` | Optional reference list of test names and descriptions. |

The source was compiled and tested with C++17/MSVC on Windows. The GCC commands above provide the Linux/WSL build path.
