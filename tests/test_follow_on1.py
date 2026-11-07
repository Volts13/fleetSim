r"""Run the PDF's Follow-on 1 example, then its Follow-on 2 history queries.

Usage: python3 tests/test_follow_on1.py ./fleet
Windows: py tests/test_follow_on1.py .\fleet.exe

The expected responses below come from the example, not from the running engine.
Ticks 5-10, omitted for space in the PDF, are sent and checked explicitly.
No third-party Python packages are required.
"""

import argparse
import json
from pathlib import Path
import subprocess
import sys


def example_steps():
    steps = []

    def add(description, request, expected):
        steps.append((description, json.loads(request), json.loads(expected)))

    # Follow-on 1: initialize, receive cargo, and commit docking.
    add("init: tick 0, empty inventories",
        '{"command":"init","parameters":{"travel_times":[[0,4,7],[4,0,3],[7,3,0]],"dock_throughput":[2,4,1],"vessels":[{"id":"v1","location":0},{"id":"v2","location":2}]}}',
        '{"status":"ok","payload":{"tick":0,"vessels":{"v1":{"state":"idle","location":0,"cargo":[]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[]}}')

    add("tick 1: two units arrive at dock 0",
        '{"command":"tick","parameters":{"cargo_manifest":[{"location":0,"destination":2,"quantity":1},{"location":0,"destination":1,"quantity":1}]}}',
        '{"status":"ok","payload":{"tick":1,"vessels":{"v1":{"state":"idle","location":0,"cargo":[]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[{"location":0,"destination":2,"quantity":1},{"location":0,"destination":1,"quantity":1}]}}')

    add("dock v1: planned dock, committed state still idle",
        '{"command":"dock","parameters":{"vessel_id":"v1"}}',
        '{"status":"ok","payload":{"tick":1,"vessels":{"v1":{"state":"docked","location":0,"cargo":[]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[{"location":0,"destination":2,"quantity":1},{"location":0,"destination":1,"quantity":1}]}}')

    add("tick 2: commit the dock",
        '{"command":"tick","parameters":{}}',
        '{"status":"ok","payload":{"tick":2,"vessels":{"v1":{"state":"docked","location":0,"cargo":[]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[{"location":0,"destination":2,"quantity":1},{"location":0,"destination":1,"quantity":1}]}}')

    # Both loads fit dock 0's throughput of two operations.
    add("load destination 2: first operation",
        '{"command":"load","parameters":{"vessel_id":"v1","destination":2}}',
        '{"status":"ok","payload":{"tick":2,"vessels":{"v1":{"state":"docked","location":0,"cargo":[{"destination":2,"quantity":1}]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[{"location":0,"destination":1,"quantity":1}]}}')

    add("load destination 1: second operation",
        '{"command":"load","parameters":{"vessel_id":"v1","destination":1}}',
        '{"status":"ok","payload":{"tick":2,"vessels":{"v1":{"state":"docked","location":0,"cargo":[{"destination":2,"quantity":1},{"destination":1,"quantity":1}]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[]}}')

    add("tick 3: commit both loads",
        '{"command":"tick","parameters":{}}',
        '{"status":"ok","payload":{"tick":3,"vessels":{"v1":{"state":"docked","location":0,"cargo":[{"destination":2,"quantity":1},{"destination":1,"quantity":1}]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[]}}')

    add("undock v1: planned idle, committed state still docked",
        '{"command":"undock","parameters":{"vessel_id":"v1"}}',
        '{"status":"ok","payload":{"tick":3,"vessels":{"v1":{"state":"idle","location":0,"cargo":[{"destination":2,"quantity":1},{"destination":1,"quantity":1}]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[]}}')

    add("tick 4: commit the undock",
        '{"command":"tick","parameters":{}}',
        '{"status":"ok","payload":{"tick":4,"vessels":{"v1":{"state":"idle","location":0,"cargo":[{"destination":2,"quantity":1},{"destination":1,"quantity":1}]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[]}}')

    add("set_destination 2: arrival at tick 4 + 7 = 11",
        '{"command":"set_destination","parameters":{"vessel_id":"v1","destination":2}}',
        '{"status":"ok","payload":{"tick":4,"vessels":{"v1":{"state":"transit","from":0,"to":2,"arrives_at_tick":11,"cargo":[{"destination":2,"quantity":1},{"destination":1,"quantity":1}]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[]}}')

    # The PDF omits these commands; tick advances by one, so all six are needed.
    for tick in range(5, 11):
        expected = json.loads(
            '{"status":"ok","payload":{"tick":5,"vessels":{"v1":{"state":"transit","from":0,"to":2,"arrives_at_tick":11,"cargo":[{"destination":2,"quantity":1},{"destination":1,"quantity":1}]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[]}}')
        expected["payload"]["tick"] = tick
        steps.append((f"tick {tick}: v1 still in transit",
                      {"command": "tick", "parameters": {}}, expected))

    add("tick 11: v1 arrives idle at location 2",
        '{"command":"tick","parameters":{}}',
        '{"status":"ok","payload":{"tick":11,"vessels":{"v1":{"state":"idle","location":2,"cargo":[{"destination":2,"quantity":1},{"destination":1,"quantity":1}]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[]}}')

    add("dock v1 at location 2 immediately after arrival",
        '{"command":"dock","parameters":{"vessel_id":"v1"}}',
        '{"status":"ok","payload":{"tick":11,"vessels":{"v1":{"state":"docked","location":2,"cargo":[{"destination":2,"quantity":1},{"destination":1,"quantity":1}]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[]}}')

    add("tick 12: commit the dock",
        '{"command":"tick","parameters":{}}',
        '{"status":"ok","payload":{"tick":12,"vessels":{"v1":{"state":"docked","location":2,"cargo":[{"destination":2,"quantity":1},{"destination":1,"quantity":1}]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[]}}')

    add("unload destination 2: delivery removes that unit from the simulation",
        '{"command":"unload","parameters":{"vessel_id":"v1","destination":2}}',
        '{"status":"ok","payload":{"tick":12,"vessels":{"v1":{"state":"docked","location":2,"cargo":[{"destination":1,"quantity":1}]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[]}}')

    add("tick 13: commit the delivery",
        '{"command":"tick","parameters":{}}',
        '{"status":"ok","payload":{"tick":13,"vessels":{"v1":{"state":"docked","location":2,"cargo":[{"destination":1,"quantity":1}]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[]}}')

    # Follow-on 2: query the past and present, reject the future, keep live state.
    add("get_state_at(1): original idle vessels and dock cargo remain in history",
        '{"command":"get_state_at","parameters":{"tick":1}}',
        '{"status":"ok","payload":{"tick":1,"vessels":{"v1":{"state":"idle","location":0,"cargo":[]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[{"location":0,"destination":2,"quantity":1},{"location":0,"destination":1,"quantity":1}]}}')

    add("get_state_at(13): current committed state includes the delivery",
        '{"command":"get_state_at","parameters":{"tick":13}}',
        '{"status":"ok","payload":{"tick":13,"vessels":{"v1":{"state":"docked","location":2,"cargo":[{"destination":1,"quantity":1}]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[]}}')

    add("get_state_at(99): future tick is rejected",
        '{"command":"get_state_at","parameters":{"tick":99}}',
        '{"status":"error","payload":{"message":"tick 99 is in the future (current tick: 13)"}}')

    add("get_state: historical queries did not change the live world",
        '{"command":"get_state","parameters":{}}',
        '{"status":"ok","payload":{"tick":13,"vessels":{"v1":{"state":"docked","location":2,"cargo":[{"destination":1,"quantity":1}]},"v2":{"state":"idle","location":2,"cargo":[]}},"cargo":[]}}')
    return steps


def compact(value):
    # Ignore formatting whitespace, but keep field and array order like the PDF.
    return json.dumps(value, separators=(",", ":"), ensure_ascii=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", help="Path to the compiled fleet program")
    args = parser.parse_args()
    executable = Path(args.executable).resolve()
    if not executable.is_file():
        parser.error(f"Executable does not exist: {executable}")

    steps = example_steps()
    commands = "".join(compact(request) + "\n" for _, request, _ in steps)
    try:
        result = subprocess.run([str(executable)], input=commands, text=True,
                                capture_output=True, timeout=10)
    except subprocess.TimeoutExpired:
        print("FAIL: the program did not finish within 10 seconds.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"FAIL: could not run the program: {error}", file=sys.stderr)
        return 1

    if result.returncode != 0:
        print(f"FAIL: program exited with code {result.returncode}.", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return 1

    lines = result.stdout.splitlines()
    if len(lines) != len(steps):
        print(f"FAIL: sent {len(steps)} commands, received {len(lines)} output lines."
              " Expected exactly one JSON response per command.", file=sys.stderr)
        print(result.stdout, file=sys.stderr)
        return 1

    failed = 0
    for number, ((description, request, expected), line) in enumerate(zip(steps, lines), 1):
        try:
            actual = json.loads(line)
        except json.JSONDecodeError:
            failed += 1
            print(f"FAIL {number:02}: {description}\n  Invalid JSON output: {line!r}")
            continue
        if compact(actual) != compact(expected):
            failed += 1
            print(f"FAIL {number:02}: {description}")
            print(f"  Command:  {compact(request)}")
            print(f"  Expected: {compact(expected)}")
            print(f"  Actual:   {compact(actual)}")
        else:
            print(f"PASS {number:02}: {description}")

    print(f"{len(steps) - failed}/{len(steps)} responses passed; {failed} failed.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
