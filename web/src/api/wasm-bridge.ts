// These describe the raw JSON shapes the WASM worker's INFER/ANALYZE_VITALS
// messages resolve with (wasm/src/bindings.cpp) -- edge-module internals,
// not part of the server REST contract in ./types, so they live here rather
// than there.
export interface InferenceResult {
  response: string;
  type?: string;
  confidence?: number;
  related_concepts?: string[];
}

export interface VitalsAnalysis {
  severity: 'info' | 'warning' | 'critical';
  alerts: string[];
  recommendations: string[];
  confidence: number;
}

export interface ScenarioStep {
  [key: string]: unknown;
}

export interface HealthStatus {
  status: string;
  message?: string;
}

// Safety net: if the worker never responds (hung wasm_init_engine, a wasm
// module that fails to instantiate silently, a message-contract mismatch,
// etc.) we must not leave the caller's promise pending forever -- that is
// what previously left the app stuck on "Initializing clinical
// interface..." with no visible error. Every request made of the worker
// gets a hard deadline; if the worker is still alive when a request times
// out it is terminated so it can't keep spinning in the background.
const DEFAULT_REQUEST_TIMEOUT_MS = 15000;

export class WasmBridge {
  private worker: Worker | null = null;
  private isInitialized: boolean = false;
  private messageId: number = 0;
  private pendingRequests: Map<number, { resolve: (val: any) => void, reject: (err: any) => void, timeoutHandle: ReturnType<typeof setTimeout> }> = new Map();

  /**
   * Send a message to the worker and resolve/reject when it responds,
   * rejecting instead if no response arrives within `timeoutMs`.
   */
  private callWorker<T>(message: { type: string; payload: any }, timeoutMs: number = DEFAULT_REQUEST_TIMEOUT_MS): Promise<T> {
    if (!this.worker) {
      return Promise.reject(new Error('WASM worker not created'));
    }
    const worker = this.worker;
    const id = this.messageId++;

    return new Promise<T>((resolve, reject) => {
      const timeoutHandle = setTimeout(() => {
        this.pendingRequests.delete(id);
        reject(new Error(`WASM worker request '${message.type}' timed out after ${timeoutMs}ms`));
        // The worker may still be stuck (e.g. an infinite loop inside the
        // wasm module). Terminate it so it stops burning CPU in the
        // background and any future calls fail fast instead of queuing up
        // behind a dead worker.
        this.destroy();
      }, timeoutMs);

      this.pendingRequests.set(id, { resolve, reject, timeoutHandle });
      worker.postMessage({ ...message, id });
    });
  }

  async loadModule(wasmUrl: string, timeoutMs: number = DEFAULT_REQUEST_TIMEOUT_MS): Promise<void> {
    try {
      this.worker = new Worker(new URL('../workers/wasm.worker.ts', import.meta.url), { type: 'module' });
    } catch (e) {
      throw e instanceof Error ? e : new Error(String(e));
    }

    this.worker.onmessage = (e) => {
      const { type, id, result, error } = e.data;

      const pending = this.pendingRequests.get(id);
      if (pending) {
        clearTimeout(pending.timeoutHandle);
        this.pendingRequests.delete(id);

        if (type === 'SUCCESS') {
          pending.resolve(result);
        } else {
          pending.reject(new Error(error));
        }
      }
    };

    this.worker.onerror = (e) => {
      // Reject every request currently in flight -- a worker-level error
      // (e.g. the module failed to parse/load) means none of them will
      // ever get a response.
      const err = new Error(`Worker error: ${e.message}`);
      for (const [id, pending] of this.pendingRequests) {
        clearTimeout(pending.timeoutHandle);
        pending.reject(err);
        this.pendingRequests.delete(id);
      }
    };

    await this.callWorker<void>({ type: 'INIT', payload: { wasmUrl } }, timeoutMs);
    this.isInitialized = true;
  }

  isReady(): boolean {
    return this.isInitialized && this.worker !== null;
  }

  health(): HealthStatus {
    if (!this.isReady()) {
      return { status: 'not_initialized' };
    }
    // Since worker health check would be async, we just return ready if worker is up
    return { status: 'ready', message: 'WASM Worker Running' };
  }

  async infer(prompt: string, maxTokens: number = 128): Promise<InferenceResult> {
    if (!this.isReady()) throw new Error('WASM module not initialized');

    return this.callWorker<InferenceResult>({
      type: 'INFER',
      payload: { action: 'infer', parameters: { prompt, max_tokens: maxTokens } }
    });
  }

  async analyzeVitals(vitals: Record<string, number>): Promise<VitalsAnalysis> {
    if (!this.isReady()) throw new Error('WASM module not initialized');

    return this.callWorker<VitalsAnalysis>({
      type: 'ANALYZE_VITALS',
      payload: vitals
    });
  }

  async simulateScenarioStep(_scenario: Record<string, any>): Promise<ScenarioStep> {
    // Training scenario logic lives entirely server-side (ScenarioRuntime in
    // src/clinical/training_scenario.cpp) and is deliberately not compiled
    // into the WASM build -- see the comment on ApiClient.executeTrainingAction
    // in api/client.ts for why an edge-native evaluator here would only ever
    // be able to fake scoring, not actually run scenario logic.
    throw new Error('Scenario stepping is not available in the WASM edge module; use the training API instead.');
  }

  echo(input: string): string {
    return input; // Simple mock for echo
  }

  destroy(): void {
    for (const [id, pending] of this.pendingRequests) {
      clearTimeout(pending.timeoutHandle);
      pending.reject(new Error('WASM worker destroyed while request was in flight'));
      this.pendingRequests.delete(id);
    }
    if (this.worker) {
      this.worker.terminate();
      this.worker = null;
    }
    this.isInitialized = false;
  }
}

let wasmInstance: WasmBridge | null = null;

export async function initializeWasm(wasmUrl: string = '/optic-trigeminal.wasm'): Promise<WasmBridge> {
  if (wasmInstance && wasmInstance.isReady()) {
    return wasmInstance;
  }

  wasmInstance = new WasmBridge();
  await wasmInstance.loadModule(wasmUrl);
  return wasmInstance;
}

export function getWasmBridge(): WasmBridge {
  if (!wasmInstance || !wasmInstance.isReady()) {
    throw new Error('WASM bridge not initialized. Call initializeWasm() first.');
  }
  return wasmInstance;
}

export function cleanupWasm(): void {
  if (wasmInstance) {
    wasmInstance.destroy();
    wasmInstance = null;
  }
}
