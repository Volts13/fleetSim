"""Run with: python3 test_engine.py ./fleet"""
import copy
import json
import subprocess
import sys
from pathlib import Path

checks = 0

def check(condition, message):
    global checks
    assert condition, message
    checks += 1

class Engine:
    def __enter__(self):
        self.process = subprocess.Popen([str(Path(sys.argv[1]).resolve())],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        return self

    def __exit__(self, *exc):
        self.process.stdin.close()
        self.process.wait(timeout=5)
        check(self.process.returncode == 0, 'process exit')

    def send(self, command, **parameters):
        self.process.stdin.write(json.dumps({'command': command, 'parameters': parameters}) + '\n')
        self.process.stdin.flush()
        self.raw = self.process.stdout.readline().rstrip('\n')
        return json.loads(self.raw)

    def ok(self, command, **parameters):
        response = self.send(command, **parameters)
        check(response['status'] == 'ok', (command, response))
        return response['payload']

    def error(self, command, **parameters):
        before = self.send('get_state')
        response = self.send(command, **parameters)
        check(response['status'] == 'error', (command, response))
        check(self.send('get_state') == before, 'rejected command changed state')
        return response

def manifest(location, destination, quantity=1):
    return {'location': location, 'destination': destination, 'quantity': quantity}

def cargo(destination, quantity=1):
    return {'destination': destination, 'quantity': quantity}

def setup(throughput=None, inventory=None, together=False):
    result = {'travel_times': [[0,4,7],[4,0,3],[7,3,0]],
              'dock_throughput': throughput or [2,4,1],
              'vessels': [{'id':'v1','location':0},{'id':'v2','location':0 if together else 2}]}
    if inventory is not None: result['cargo_manifest'] = inventory
    return result

def vessel(state='idle', location=0, inventory=None, arrival=None):
    result = {'state': state}
    if state == 'transit':
        result.update({'from': location, 'to': 2, 'arrives_at_tick': arrival})
    else: result['location'] = location
    result['cargo'] = inventory or []
    return result

# Every response in the PDF's full Follow-on 1 example, including omitted ticks.
with Engine() as e:
    def example(command, tick, v1, dock_cargo, **parameters):
        e.ok(command, **parameters)
        expected = {'status':'ok','payload':{'tick':tick,
            'vessels':{'v1':v1,'v2':vessel(location=2)}, 'cargo':dock_cargo}}
        check(e.raw == json.dumps(expected, separators=(',',':')), 'PDF output/order: ' + command)
    dock_cargo = [manifest(0,2), manifest(0,1)]
    full = [cargo(2), cargo(1)]
    example('init', 0, vessel(), [], **setup())
    example('tick', 1, vessel(), dock_cargo, cargo_manifest=dock_cargo)
    example('dock', 1, vessel('docked'), dock_cargo, vessel_id='v1')
    example('tick', 2, vessel('docked'), dock_cargo)
    example('load', 2, vessel('docked', inventory=[cargo(2)]), [manifest(0,1)], vessel_id='v1', destination=2)
    example('load', 2, vessel('docked', inventory=full), [], vessel_id='v1', destination=1)
    example('tick', 3, vessel('docked', inventory=full), [])
    example('undock', 3, vessel(inventory=full), [], vessel_id='v1')
    example('tick', 4, vessel(inventory=full), [])
    example('set_destination', 4, vessel('transit', inventory=full, arrival=11), [], vessel_id='v1', destination=2)
    for tick in range(5,11): example('tick', tick, vessel('transit', inventory=full, arrival=11), [])
    example('tick', 11, vessel(location=2, inventory=full), [])
    example('dock', 11, vessel('docked', 2, full), [], vessel_id='v1')
    example('tick', 12, vessel('docked', 2, full), [])
    example('unload', 12, vessel('docked', 2, [cargo(1)]), [], vessel_id='v1', destination=2)
    example('tick', 13, vessel('docked', 2, [cargo(1)]), [])

# Shared throughput; failed ticks must not reset it or commit planned changes.
with Engine() as e:
    e.ok('init', **setup(inventory=[manifest(0,1,5),manifest(0,2,5)], together=True))
    for id in ['v1','v2']: e.ok('dock', vessel_id=id)
    e.error('load', vessel_id='v1', destination=1)  # pending dock is insufficient
    e.ok('tick')
    e.ok('load', vessel_id='v1', destination=1)
    e.ok('load', vessel_id='v2', destination=2)
    rejected = e.error('unload', vessel_id='v1', destination=1)
    check(rejected['payload']['message'] == 'transfer of 3 operations exceeds dock throughput of 2 at location 0', 'throughput message')
    e.error('tick', cargo_manifest=[manifest(0,1),manifest(0,3)])
    e.error('unload', vessel_id='v1', destination=1)
    e.ok('tick')
    e.ok('unload', vessel_id='v1', destination=1)
    e.ok('load', vessel_id='v1', destination=1)
    e.error('load', vessel_id='v2', destination=1)  # unload + load cost two
    state = e.ok('undock', vessel_id='v1')
    check(state['vessels']['v1']['cargo'] == [cargo(1)], 'undock retains planned cargo')
    e.error('load', vessel_id='v1', destination=1)
    e.error('unload', vessel_id='v1', destination=1)
    e.error('swap', vessel_id='v1', load_destination=2, unload_destination=1)
    e.ok('undock', vessel_id='v1')  # repeating a pending undock is idempotent
    e.error('dock', vessel_id='v1')
    e.error('set_destination', vessel_id='v1', destination=2)
    e.ok('tick')
    e.ok('set_destination', vessel_id='v1', destination=2)

# Swaps require both sources, are atomic, and consume just one operation.
with Engine() as e:
    e.ok('init', **setup([1,4,1], [manifest(0,2),manifest(0,1)]))
    e.ok('dock', vessel_id='v1'); e.ok('tick')
    e.ok('load', vessel_id='v1', destination=2); e.ok('tick')
    e.error('swap', vessel_id='v1', load_destination=2, unload_destination=2)
    e.error('swap', vessel_id='v1', load_destination=1, unload_destination=0)
    state = e.ok('swap', vessel_id='v1', load_destination=1, unload_destination=2)
    check(state['vessels']['v1']['cargo'] == [cargo(1)], 'swap vessel inventory')
    check(state['cargo'] == [manifest(0,2)], 'swap dock inventory')
    e.error('swap', vessel_id='v1', load_destination=2, unload_destination=1)
    e.ok('tick')
    e.ok('swap', vessel_id='v1', load_destination=2, unload_destination=1)

# Identical-destination swaps still cost one operation; delivery also applies in swaps.
with Engine() as e:
    e.ok('init', **setup([2,4,1], [manifest(0,0,2),manifest(0,1,2)]))
    e.ok('dock', vessel_id='v1'); e.ok('tick')
    e.ok('load', vessel_id='v1', destination=0)
    e.ok('load', vessel_id='v1', destination=1)
    before = e.ok('tick')
    state = e.ok('swap', vessel_id='v1', load_destination=1, unload_destination=1)
    check(state == before, 'identical swap preserves inventory and order')
    state = e.ok('swap', vessel_id='v1', load_destination=0, unload_destination=0)
    check(state['cargo'] == [manifest(0,1)], 'identical swap delivers one unit')
    e.error('load', vessel_id='v1', destination=1)

# Dock and destination override each other only before commitment.
with Engine() as e:
    e.ok('init', **setup())
    e.ok('set_destination', vessel_id='v1', destination=2)
    e.ok('dock', vessel_id='v1'); e.ok('dock', vessel_id='v1')
    e.error('undock', vessel_id='v1')
    e.ok('set_destination', vessel_id='v1', destination=1)
    e.ok('dock', vessel_id='v1'); e.ok('tick')
    e.error('set_destination', vessel_id='v1', destination=2)
    e.error('dock', vessel_id='v1')
    for _ in range(7): e.ok('tick')
    check(e.ok('get_state')['vessels']['v1']['state'] == 'docked', 'discarded trip never arrives')
    e.ok('undock', vessel_id='v1'); e.ok('tick')
    e.ok('set_destination', vessel_id='v1', destination=0)
    check(e.ok('tick')['vessels']['v1']['state'] == 'idle', 'zero-distance trip')

# Optional and duplicate manifests, accumulation, and rejected-input atomicity.
bad_parameters = []
for throughput in [None, [], [1,1], [0,1,1], [True,1,1], [1.5,1,1]]:
    p=setup(); p['dock_throughput']=throughput; bad_parameters.append(p)
p=setup(); del p['dock_throughput']; bad_parameters.append(p)
for bad in [None, {}, [manifest(-1,1)], [manifest(0,3)], [manifest(0,True)],
            [manifest(0,1,0)], [manifest(0,1,1.5)], [manifest(0,1,True)],
            [manifest(0,1,2**63)], [manifest(0,1),{}]]:
    p=setup(); p['cargo_manifest']=bad; bad_parameters.append(p)
for p in bad_parameters:
    with Engine() as e:
        e.error('init', **p)
        check(e.ok('init', **setup())['cargo'] == [], 'failed init leaves no cargo')
with Engine() as e:
    state=e.ok('init', **setup(inventory=[manifest(0,2),manifest(0,1),manifest(0,2,2)]))
    check(state['cargo']==[manifest(0,2,3),manifest(0,1)], 'duplicate cargo merged in insertion order')
    check(e.ok('tick')['cargo']==state['cargo'], 'absent manifest retains cargo')
    check(e.ok('tick', cargo_manifest=[])['cargo']==state['cargo'], 'empty manifest retains cargo')
    check(e.ok('tick', cargo_manifest=[manifest(0,2)])['cargo'][0]['quantity']==4, 'manifest adds cargo')
    e.error('tick', cargo_manifest=[manifest(0,2),manifest(8,1)])
    for cmd in ['dock','undock','load','unload','swap']:
        e.error(cmd, vessel_id='missing', destination=1, load_destination=1, unload_destination=2)

# Counter overflow must reject a manifest without advancing time or partially adding cargo.
with Engine() as e:
    e.ok('init', **setup(inventory=[manifest(0,1,2**63-1)]))
    e.error('tick', cargo_manifest=[manifest(0,2),manifest(0,1)])
    e.ok('dock', vessel_id='v1'); e.ok('tick')
    e.ok('load', vessel_id='v1', destination=1)
    e.ok('tick', cargo_manifest=[manifest(0,1)])
    e.error('unload', vessel_id='v1', destination=1)
    e.ok('load', vessel_id='v1', destination=1)  # failed unload spent no throughput

with Engine() as e:
    p=setup(); p['vessels']=[{'id':id,'location':0} for id in ['zeta','v2','alpha','v10']]
    check(list(e.ok('init', **p)['vessels'])==['zeta','v2','alpha','v10'], 'vessel initialization order')
with Engine() as e:
    p=setup(); p['vessels']=[]
    check(e.ok('init', **p)['vessels']=={}, 'empty fleet object')
    check(e.ok('tick')['cargo']==[], 'empty dock cargo array')
with Engine() as e:
    e.process.stdin.write('{bad json}\n'); e.process.stdin.flush()
    check(json.loads(e.process.stdout.readline())['status']=='error', 'malformed JSON response')
    e.ok('init', **setup())
    e.error('unknown')

print(f'{checks} checks passed, including exact PDF output ordering and dock/cargo edge cases.')
