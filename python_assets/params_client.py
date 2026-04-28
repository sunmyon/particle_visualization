import json

import zmq


class ParamsClient:
    def __init__(self, endpoint="tcp://127.0.0.1:5557"):
        self.c = zmq.Context.instance().socket(zmq.REQ)
        self.c.connect(endpoint)

    def _x(self, payload):
        self.c.send_string(json.dumps(payload))
        return json.loads(self.c.recv_string())

    def ping(self):
        return self._x({"cmd": "ping"})

    def edit(self, fields, start=None, count=None):
        payload = {"cmd": "edit", "fields": list(fields)}
        if start is not None:
            payload["start"] = int(start)
        if count is not None:
            payload["count"] = int(count)
        return self._x(payload)
