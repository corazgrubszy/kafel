"""Small independent wire peer for offline daemon integration tests, not production."""
import contextlib
import hashlib
import json
from pathlib import Path
import socket
import struct

def read_exact(stream, size):
    result = bytearray()
    while len(result) < size:
        part = stream.recv(size - len(result))
        if not part:
            raise EOFError('incomplete DASH/5 frame')
        result.extend(part)
    return bytes(result)

def read_frame(stream):
    size, = struct.unpack('!I', read_exact(stream, 4))
    assert 0 < size <= 65536
    return json.loads(read_exact(stream, size))

def send_frame(stream, value):
    payload = json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode()
    stream.sendall(struct.pack('!I', len(payload)) + payload)

@contextlib.contextmanager
def listener(path):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
        server.bind(str(path)); server.listen(1); server.settimeout(8)
        yield server

def collect(server, count=2):
    """Verify bytes/hashes, template dependencies, complete snapshots and heartbeat."""
    with server.accept()[0] as stream:
        stream.settimeout(8)
        assert read_exact(stream, 7) == b'DASH/5\n'
        hello = read_frame(stream)
        assert hello['type'] == 'hello' and len(hello['instance']) == 32
        stream.sendall(b'DASH/5\n')
        ready=json.loads((Path(__file__).resolve().parent.parent/'docs/dash5/weather/ready.json').read_text())
        send_frame(stream, ready)
        resources = {}; snapshots = []; active = None
        while len(snapshots) < count:
            message = read_frame(stream)
            if message['type'] == 'put':
                data = read_exact(stream, message['bytes'])
                assert message['id'] == 'sha256:' + hashlib.sha256(data).hexdigest()
                resources[message['id']] = (message['media_type'], data)
                send_frame(stream, {'type': 'stored', 'id': message['id']})
            elif message['type'] == 'publish':
                missing = set()
                for tile in message['tiles']:
                    if tile['content'] is None:
                        assert tile['state'] == 'error' and tile['freshness'] == 'missing'
                        continue
                    content = tile['content']; template_id = content['template']
                    if template_id not in resources:
                        missing.add(template_id); continue
                    template = json.loads(resources[template_id][1])
                    assert template['schema'] == 'kafel.tile/1'
                    assert set(content['data']) == {field['name'] for field in template['fields']}
                    for field in template['fields']:
                        if field['format'] == 'svg' and content['data'][field['name']] not in resources:
                            missing.add(content['data'][field['name']])
                if missing:
                    send_frame(stream, {'type': 'rejected', 'operation': 'publish', 'code': 'missing_resources',
                                        'revision': message['revision'], 'resources': sorted(missing)})
                else:
                    active = message; snapshots.append(message['tiles'])
                    send_frame(stream, {'type': 'accepted', 'operation': 'publish', 'revision': message['revision']})
            elif message['type'] == 'ping':
                assert active and message['revision'] == active['revision']
                snapshots.append(active['tiles'])
                send_frame(stream, {'type': 'accepted', 'operation': 'ping', 'revision': message['revision']})
            else:
                raise AssertionError(message)
        return snapshots, resources
