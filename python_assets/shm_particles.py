from multiprocessing import shared_memory
import numpy as np, struct

DT = {1: np.float32, 2: np.float64, 3: np.uint64, 4: np.uint32, 5: np.uint8}
HDR=64; ENTRY=32

F_POS = 0
F_VEL = 1
F_B = 2
F_DENS = 3
F_TEMP = 4
F_MASS = 5
F_HSML = 6
F_VAL = 7
F_VAL2 = 8
F_ID = 9
F_TYPE = 10
F_ORIGPOS = 11
F_MASK = 12
F_FLAG = 13


class ShmParticles:
    def __init__(self, name="cppvis_pos"):
        try:
            self.shm = shared_memory.SharedMemory(name=name if name.startswith('/') else '/'+name)
        except FileNotFoundError:
            self.shm = shared_memory.SharedMemory(name=name)
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

    def optional(self, fid):
        return self.view(fid) if fid in self.fields else None

    @property
    def pos(self):     return self.view(F_POS)
    @property
    def vel(self):     return self.view(F_VEL)
    @property
    def B(self):       return self.optional(F_B)
    @property
    def dens(self):    return self.view(F_DENS)
    @property
    def temp(self):    return self.view(F_TEMP)
    @property
    def mass(self):    return self.view(F_MASS)
    @property
    def hsml(self):    return self.view(F_HSML)
    @property
    def val(self):     return self.view(F_VAL)
    @property
    def val2(self):    return self.view(F_VAL2)
    @property
    def id(self):      return self.view(F_ID)
    @property
    def type(self):    return self.view(F_TYPE)
    @property
    def origpos(self): return self.view(F_ORIGPOS)
    @property
    def mask(self):    return self.view(F_MASK)
    @property
    def flag(self):    return self.view(F_FLAG)
