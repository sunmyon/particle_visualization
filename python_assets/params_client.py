import zmq, json
class ParamsClient:
    def __init__(self, endpoint="tcp://127.0.0.1:5557"):
        self.c = zmq.Context.instance().socket(zmq.REQ); self.c.connect(endpoint)
    def _x(self, d): self.c.send_string(json.dumps(d)); return json.loads(self.c.recv_string())
    def get(self):  return self._x({"cmd":"get_params"})["params"]
    def set(self, **p): return self._x({"cmd":"set_params","params":p})
    def begin(self):    return self._x({"cmd":"begin_edit"})
    def end(self, dirty=None):
        d={"cmd":"end_edit"}; 
        if dirty: d["dirty"]=dirty
        return self._x(d)
    def scale(self, factor): return self._x({"cmd":"scale","factor":factor})
    def center(self):        return self._x({"cmd":"center"})
