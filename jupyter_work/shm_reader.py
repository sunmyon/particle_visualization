# shm_reader.py  — POSIX shm とファイルmmap 両対応
import os, sys, mmap, ctypes, struct
import numpy as np

# ---- C と同じ定義 ----
DT_FLOAT32=1; DT_FLOAT64=2; DT_UINT64=3; DT_UINT32=4; DT_UINT8=5
_HDR_FMT = "<IIQIIII"     # ShmHeader (pack(1), 32B) magic,version,countN,n_fields,field_mask,flags,reserved
_HDR_SIZE = struct.calcsize(_HDR_FMT)
_ENT_FMT = "<IIIIQQ"      # FieldEntry (pack(1), 32B)
_ENT_SIZE = struct.calcsize(_ENT_FMT)

def _align64(x: int) -> int: return (x + 63) & ~63

def _np_dtype(dt):
    return {DT_FLOAT32: np.float32, DT_FLOAT64: np.float64,
            DT_UINT64:  np.uint64,  DT_UINT32:  np.uint32,
            DT_UINT8:   np.uint8}[dt]

class _BaseReader:
    def __init__(self):
        self.mm = None
        self.N = 0
        self.fields = {}
        self.arrays = {}

    def _parse_layout(self):
        # ヘッダ
        hdr = self.mm[:_HDR_SIZE]
        magic, version, countN, n_fields, field_mask, flags, reserved = struct.unpack(_HDR_FMT, hdr)
        if magic != 0xC0FFEE01:
            raise ValueError(f"Bad magic: {hex(magic)}")
        self.N = countN

        # FieldEntry 配列
        ent_off = _align64(_HDR_SIZE)
        base = memoryview(self.mm)
        for i in range(n_fields):
            off = ent_off + i*_ENT_SIZE
            field_id, dtype, ndim, comps, offset, nbytes = struct.unpack(_ENT_FMT, self.mm[off:off+_ENT_SIZE])
            self.fields[field_id] = dict(dtype=dtype, ndim=ndim, comps=comps, offset=offset, bytes=nbytes)

        # numpy view
        for fid, meta in self.fields.items():
            mv = base[meta["offset"] : meta["offset"] + meta["bytes"]]
            arr = np.frombuffer(mv, dtype=_np_dtype(meta["dtype"]))
            if meta["ndim"] == 2 and meta["comps"] > 1:
                expect = self.N * meta["comps"]
                if arr.size >= expect:
                    arr = arr[:expect].reshape(self.N, meta["comps"])
            else:
                expect = self.N
                if arr.size >= expect:
                    arr = arr[:expect]
            self.arrays[fid] = arr

    def array(self, field_id: int):
        return self.arrays.get(field_id, None)

    def close(self):
        if self.mm is not None:
            self.mm.close()
            self.mm = None

# ---- POSIX shm (macOS/Linux/WSL2) ----
# macOS でも ctypes の shm_open を使う
libc = ctypes.CDLL(None)
libc.shm_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
libc.shm_open.restype  = ctypes.c_int
O_RDONLY=0; O_RDWR=2; O_CREAT=0o100

class ShmReader(_BaseReader):
    """名前付き共有メモリを開く /cppvis_pos のような名前を渡す"""
    def __init__(self, name: str):
        super().__init__()
        if not name.startswith("/"): name = "/" + name
        fd = libc.shm_open(name.encode("utf-8"), O_RDWR, 0o600)
        if fd < 0:
            raise OSError(f"shm_open failed for {name}")
        try:
            st = os.fstat(fd)
            size = st.st_size
            if size <= 0:
                raise OSError("shared memory size is zero")
            self.mm = mmap.mmap(fd, size, flags=mmap.MAP_SHARED, prot=(mmap.PROT_READ|mmap.PROT_WRITE))
        finally:
            os.close(fd)
        self._parse_layout()

# ---- ファイル mmap フォールバック ----
class ShmReaderFile(_BaseReader):
    """C++ が /tmp/foo.mm のようにファイルmmapフォールバックした時用"""
    def __init__(self, path: str):
        super().__init__()
        if not os.path.isfile(path):
            raise OSError(f"mmap file not found: {path}")
        # 0 サイズの可能性を避けるため size を取得
        size = os.path.getsize(path)
        f = open(path, "r+b")
        try:
            self.mm = mmap.mmap(f.fileno(), size, flags=mmap.MAP_SHARED, prot=(mmap.PROT_READ|mmap.PROT_WRITE))
        finally:
            f.close()
        self._parse_layout()

# ---- 便利ヘルパ：自動でPOSIX→ファイルの順に試す ----
def open_shared(name="/cppvis_pos", fallback_file=None):
    try:
        return ShmReader(name)
    except Exception as e:
        if fallback_file and os.path.exists(fallback_file):
            return ShmReaderFile(fallback_file)
        raise
