/// <reference lib="webworker" />

interface WasmModule {
  exports: {
    memory: WebAssembly.Memory;
    _initialize: () => void;
    wasm_init_engine: () => number;
    wasm_destroy_engine: () => void;
    wasm_infer: (input_ptr: number, input_len: number) => number;
    wasm_analyze_vitals: (input_ptr: number, input_len: number) => number;
    wasm_health: () => number;
    wasm_echo: (input_ptr: number, input_len: number) => number;
    malloc: (size: number) => number;
    free: (ptr: number) => void;
  };
}

let wasmModule: WasmModule | null = null;
const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();

// Basic WASI stubs. clock_time_get and random_get previously returned 0
// unconditionally -- that 0 is the WASI *errno* (success), not the value:
// per the WASI ABI, the actual timestamp/random bytes are written into wasm
// linear memory at the pointer the caller passed. Stubbing them as no-ops
// left that memory untouched, so the C++ side read whatever uninitialized
// bytes happened to be sitting there as "the current time" -- reproducibly
// (same build, same memory layout) decoding to a bogus date like
// "2262-04-11T23:50:08Z" every single call, not a one-off glitch.
const imports = {
  wasi_snapshot_preview1: {
    clock_time_get: (_clockId: number, _precision: bigint, timePtr: number): number => {
      if (!wasmModule) return 0;
      const nowNs = BigInt(Date.now()) * 1000000n; // ms -> ns, per WASI's timestamp unit
      new DataView(wasmModule.exports.memory.buffer).setBigUint64(timePtr, nowNs, true);
      return 0;
    },
    environ_sizes_get: () => 0,
    environ_get: () => 0,
    fd_close: () => 0,
    // Was a no-op returning success (0) without touching memory or emitting
    // anything -- every std::cout/printf/std::cerr call from the C++ side
    // silently vanished, with WASI reporting "wrote 0 bytes successfully"
    // regardless of what was actually passed. That made it impossible to
    // see engine-side diagnostics (e.g. "Processed N examples...",
    // exceptions logged via std::cerr) from inside this worker, which is
    // exactly the visibility needed to debug wasm_init_engine() hangs.
    // Real WASI contract: iovs_ptr points to iovs_len {buf_ptr, buf_len}
    // pairs (8 bytes each); write the referenced bytes out and store the
    // total byte count at nwritten_ptr.
    fd_write: (fd: number, iovsPtr: number, iovsLen: number, nwrittenPtr: number): number => {
      if (!wasmModule) return 0;
      const mem = wasmModule.exports.memory.buffer;
      const view = new DataView(mem);
      let total = 0;
      let text = '';
      for (let i = 0; i < iovsLen; i++) {
        const bufPtr = view.getUint32(iovsPtr + i * 8, true);
        const bufLen = view.getUint32(iovsPtr + i * 8 + 4, true);
        if (bufLen > 0) {
          text += textDecoder.decode(new Uint8Array(mem, bufPtr, bufLen));
        }
        total += bufLen;
      }
      if (text.length > 0) {
        if (fd === 2) console.error('[wasm]', text.replace(/\n$/, ''));
        else console.log('[wasm]', text.replace(/\n$/, ''));
      }
      view.setUint32(nwrittenPtr, total, true);
      return 0;
    },
    fd_seek: () => 0,
    fd_read: () => 0,
    random_get: (bufPtr: number, bufLen: number): number => {
      if (!wasmModule) return 0;
      const bytes = new Uint8Array(wasmModule.exports.memory.buffer, bufPtr, bufLen);
      crypto.getRandomValues(bytes);
      return 0;
    },
  },
  env: {
    emscripten_notify_memory_growth: () => 0,
  }
};

self.onmessage = async (e: MessageEvent) => {
  const { type, id, payload } = e.data;

  try {
    switch (type) {
      case 'INIT': {
        const { wasmUrl } = payload;
        const response = await fetch(wasmUrl);
        if (!response.ok) throw new Error(`Fetch failed: ${response.status}`);
        
        const buffer = await response.arrayBuffer();
        const { instance } = await WebAssembly.instantiate(buffer, imports);
        wasmModule = instance as any as WasmModule;
        
        if (typeof (wasmModule as any).exports._initialize === 'function') {
          try { (wasmModule as any).exports._initialize(); } catch (err) { console.warn('_initialize', err); }
        }

        const initResult = wasmModule.exports.wasm_init_engine();
        if (initResult !== 0) throw new Error(`Engine init failed: ${initResult}`);
        
        self.postMessage({ type: 'SUCCESS', id });
        break;
      }

      case 'INFER': {
        if (!wasmModule) throw new Error('WASM not initialized');
        const { action, parameters, patient_id, context_window } = payload;
        
        const jsonStr = JSON.stringify({ action, parameters, patient_id, context_window });
        const bytes = textEncoder.encode(jsonStr);
        
        const ptr = wasmModule.exports.malloc(bytes.length);
        const memArray = new Uint8Array(wasmModule.exports.memory.buffer);
        memArray.set(bytes, ptr);
        
        const resultPtr = wasmModule.exports.wasm_infer(ptr, bytes.length);
        wasmModule.exports.free(ptr);
        
        if (resultPtr === 0) throw new Error('WASM infer returned null');
        
        const resultMem = new Uint8Array(wasmModule.exports.memory.buffer);
        let len = 0;
        while (resultMem[resultPtr + len] !== 0) len++;
        
        const resultJson = textDecoder.decode(resultMem.slice(resultPtr, resultPtr + len));
        self.postMessage({ type: 'SUCCESS', id, result: JSON.parse(resultJson) });
        break;
      }

      case 'ANALYZE_VITALS': {
        if (!wasmModule) throw new Error('WASM not initialized');
        const { heart_rate, blood_pressure, spo2, temperature, respiratory_rate } = payload;
        
        const jsonStr = JSON.stringify({ heart_rate, blood_pressure, spo2, temperature, respiratory_rate });
        const bytes = textEncoder.encode(jsonStr);
        
        const ptr = wasmModule.exports.malloc(bytes.length);
        const memArray = new Uint8Array(wasmModule.exports.memory.buffer);
        memArray.set(bytes, ptr);
        
        const resultPtr = wasmModule.exports.wasm_analyze_vitals(ptr, bytes.length);
        wasmModule.exports.free(ptr);
        
        if (resultPtr === 0) throw new Error('WASM analyze_vitals returned null');
        
        const resultMem = new Uint8Array(wasmModule.exports.memory.buffer);
        let len = 0;
        while (resultMem[resultPtr + len] !== 0) len++;
        
        const resultJson = textDecoder.decode(resultMem.slice(resultPtr, resultPtr + len));
        self.postMessage({ type: 'SUCCESS', id, result: JSON.parse(resultJson) });
        break;
      }

      default:
        throw new Error(`Unknown message type: ${type}`);
    }
  } catch (error: any) {
    self.postMessage({ type: 'ERROR', id, error: error.message || error.toString() });
  }
};
