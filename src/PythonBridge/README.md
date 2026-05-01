# Python Bridge

## English

The Python bridge lets Python inspect and edit the particles currently loaded in
`particle_vis`.

It has two channels:

1. Shared memory stores particle arrays in a zero-copy, struct-of-arrays layout.
2. A small ZeroMQ REP server receives edit notifications from Python.

Python edits the shared memory arrays directly, then sends an `edit` RPC so the
GUI knows which fields must be copied back into `SimulationDataset`.

```text
particle_vis
  PythonBridge
    PythonBridgeSharedMemory  -> /cppvis_pos by default
    PythonBridgeRpcServer     -> tcp://127.0.0.1:5557 by default
    BridgeAdapter             -> SimulationDataset <-> shared arrays

Python
  ShmParticles                -> numpy views into shared memory
  ParamsClient.edit([...])    -> notifies C++ about changed fields
```

### Field Names

The RPC field names are:

| Name | Meaning | Shape | Dtype |
| --- | --- | --- | --- |
| `pos` | Normalized particle position used by the GUI camera/rendering. Editing this moves particles and is converted back to original snapshot coordinates internally. | `(N, 3)` | `float32` |
| `origpos` | Original particle position loaded from the snapshot. | `(N, 3)` | `float32` |
| `vel` | Velocity. | `(N, 3)` | `float32` |
| `B` | Magnetic field, present only when enabled. | `(N, 3)` | `float32` |
| `dens` | Density. | `(N,)` | `float32` |
| `temp` | Temperature. | `(N,)` | `float32` |
| `mass` | Mass. | `(N,)` | `float32` |
| `hsml` | Smoothing length. | `(N,)` | `float32` |
| `val` | User scalar value. | `(N,)` | `float32` |
| `val2` | Second user scalar value. | `(N,)` | `float32` |
| `id` | Particle ID. | `(N,)` | `uint64` |
| `type` | Particle type. | `(N,)` | `uint8` |
| `mask` | Visibility mask, where `0` means visible and `1` means hidden. | `(N,)` | `uint8` |
| `flag` | Per-particle flag. | `(N,)` | `uint8` |

### RPC Commands

`ping` checks that the RPC server is alive.

```json
{"cmd": "ping"}
```

`edit` tells C++ which arrays were modified.

```json
{"cmd": "edit", "fields": ["pos", "mask"], "start": 0, "count": 100}
```

`start` and `count` are currently diagnostic metadata. C++ still applies the
whole dirty field.

### Smoke Test

Start `particle_vis`, launch the Python bridge from the GUI, then run:

```bash
python3 python_assets/python_bridge_smoke_test.py --summary
```

To move all current positions by a tiny offset and notify the GUI:

```bash
python3 python_assets/python_bridge_smoke_test.py --offset-pos 0.01 0.0 0.0
```

To hide the first particle:

```bash
python3 python_assets/python_bridge_smoke_test.py --hide-first
```

Python dependencies are `numpy` and `pyzmq`.

## 日本語

Python bridge は、`particle_vis` に読み込まれている粒子を Python から参照・編集するための仕組みです。

通信経路は 2 つあります。

1. 共有メモリに粒子配列を SoA 形式で置き、Python から numpy view として直接触ります。
2. ZeroMQ の小さな RPC server に「どの field を編集したか」を通知します。

つまり、実データは共有メモリで編集し、`edit` RPC は GUI 側へ dirty field を知らせるためだけに使います。

### 重要な考え方

`pos` は GUI のカメラ・描画が使う正規化座標です。Python から `pos` を編集した場合、C++ 側では元 snapshot 座標へ変換して保存します。

`origpos` は snapshot から読んだ元座標です。解析・物理量との対応を保ちたい場合はこちらを基準にしてください。必要なら編集通知も可能です。

`mask` は表示マスクです。`0` が表示、`1` が非表示です。

### 最小テスト

GUI から Python bridge を起動した状態で、別 terminal から以下を実行します。

```bash
python3 python_assets/python_bridge_smoke_test.py --summary
```

粒子位置を少し動かす場合:

```bash
python3 python_assets/python_bridge_smoke_test.py --offset-pos 0.01 0.0 0.0
```

先頭粒子を非表示にする場合:

```bash
python3 python_assets/python_bridge_smoke_test.py --hide-first
```

必要な Python package は `numpy` と `pyzmq` です。
