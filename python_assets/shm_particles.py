from multiprocessing import shared_memory
import numpy as np, struct
DT = {1: np.float32, 2: np.float64, 3: np.uint64, 4: np.uint32, 5: np.uint8}
HDR=64; ENTRY=32

class ShmParticles:
    def __init__(self, name="cppvis_pos"):
        try:
            self.shm = shared_memory.SharedMemory(name=name if name.startswith('/') else '/'+name)
        except FileNotFoundError:
            self.shm = shared_memory.SharedMemory(name=name)  # Windows互換用
        self.buf = self.shm.buf
        self._parse()

    def _parse(self):
        magic, ver, N, nf, mask, flags, _ = struct.unpack_from("<IIQIIII", self.buf, 0)
        assert magic==0xC0FFEE01
        self.N=N; self.fields={}
        for i in range(nf):
            fid, dt, ndim, comps, off, size = struct.unpack_from("<IIIIQQ", self.buf, HDR + i*ENTRY)
            shape = (N, comps) if comps>1 else (N,)
            self.fields[fid] = (DT[dt], shape, off)
    def view(self, fid):
        dt, shape, off = self.fields[fid]
        return np.ndarray(shape, dtype=dt, buffer=self.buf, offset=off)
    @property
    def pos(self):     return self.view(0)
    @property
    def vel(self):     return self.view(1)
    @property
    def dens(self):    return self.view(3)
    @property
    def temp(self):    return self.view(4)
    @property
    def id(self):      return self.view(9)
    @property
    def type(self):    return self.view(10)
    @property
    def ORIGPOS(self): return self.view(11)
