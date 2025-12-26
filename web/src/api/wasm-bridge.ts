/**
 * WASM Bridge - JavaScript Interface to OpticTrigeminal WASM Module
 * 
 * Enforces memory ownership contract:
 * - Always call wasm_free() after reading results
 * - Validate JSON schemas before use
 * - Handle exceptions gracefully
 * - Enforce static buffer size limits (65KB)
 * 
 * This is Phase 1: Minimal wrapper following BINDINGS_CONTRACT.md
 */

// ============================================================================
// Type Definitions
// ============================================================================

export interface WasmModule {
  exports: {
    memory: WebAssembly.Memory;
    wasm_init_engine(): number;
    wasm_destroy_engine(): void;
    wasm_infer(requestJsonPtr: number, maxTokens: number): number;
    wasm_analyze_vitals(vitalsJsonPtr: number): number;
    wasm_simulate_scenario_step(scenarioJsonPtr: number): number;
    wasm_free(ptr: number): void;
    wasm_health(): number;
    wasm_echo(inputPtr: number): number;
    wasm_version(): number;
    wasm_build_date(): number;
    malloc(size: number): number;
    free(ptr: number): void;
  };
}

export interface InferenceResult {
  prompt: string;
  response: string;
  type: string;
  timestamp: string;
  confidence: number;
  related_concepts: string[];
  reasoning_steps?: string[];
}

export interface VitalsAnalysis {
  severity: 'info' | 'warning' | 'critical';
  alerts: Array<{
    vital: string;
    value: number;
    threshold: number;
    status: string;
  }>;
  recommendations: string[];
  confidence: number;
}

export interface ScenarioStep {
  scenario_id: string;
  status: string;
  events: Array<{
    type: string;
    data: string;
  }>;
  immutable: boolean;
}

export interface HealthStatus {
  status: 'ready' | 'not_initialized' | 'error';
  vocab_size?: number;
  graph_nodes?: number;
  training_records?: number;
  uptime_ms?: number;
  message?: string;
}

// ============================================================================
// WASM Bridge Class
// ============================================================================

export class WasmBridge {
  private wasmModule: WasmModule | null = null;
  private isInitialized: boolean = false;
  private readonly MAX_RESULT_SIZE = 65536; // 65KB buffer size

  /**
   * Load WASM module from URL
   */
  async loadModule(wasmUrl: string): Promise<void> {
    try {
      const response = await fetch(wasmUrl);
      if (!response.ok) {
        throw new Error(`Failed to fetch WASM: ${response.statusText}`);
      }

      const buffer = await response.arrayBuffer();
      const wasmModule = await WebAssembly.instantiate(buffer);
      
      this.wasmModule = wasmModule as any as WasmModule;
      
      // Initialize engine
      const result = this.wasmModule.exports.wasm_init_engine();
      if (result !== 0) {
        throw new Error(`Engine initialization failed: code ${result}`);
      }

      this.isInitialized = true;
    } catch (error) {
      throw new Error(`Failed to load WASM module: ${error}`);
    }
  }

  /**
   * Check if WASM is loaded and ready
   */
  isReady(): boolean {
    return this.isInitialized && this.wasmModule !== null;
  }

  /**
   * Health check - verify WASM engine is working
   */
  health(): HealthStatus {
    if (!this.isReady()) {
      return { status: 'not_initialized' };
    }

    try {
      const ptr = this.wasmModule!.exports.wasm_health();
      const json = this.ptrToString(ptr);
      this.wasmModule!.exports.wasm_free(ptr);
      return JSON.parse(json);
    } catch (error) {
      return {
        status: 'error',
        message: `Health check failed: ${error}`
      };
    }
  }

  /**
   * Run inference on a prompt
   * 
   * Contract: Result is allocated in static buffer, must call wasm_free
   */
  infer(prompt: string, maxTokens: number = 128): InferenceResult {
    if (!this.isReady()) {
      throw new Error('WASM module not initialized');
    }

    const requestJson = JSON.stringify({
      prompt,
      max_tokens: maxTokens,
      session_id: 'browser-session',
      temperature: 0.7
    });

    const requestPtr = this.stringToPtr(requestJson);
    
    try {
      const resultPtr = this.wasmModule!.exports.wasm_infer(requestPtr, maxTokens);
      const resultJson = this.ptrToString(resultPtr);
      
      // CRITICAL: Always free WASM result after reading
      this.wasmModule!.exports.wasm_free(resultPtr);
      
      const result = JSON.parse(resultJson);
      this.validateInferenceResult(result);
      return result;

    } finally {
      // Free input pointer
      this.wasmModule!.exports.free(requestPtr);
    }
  }

  /**
   * Analyze patient vital signs
   * 
   * Contract: Result is allocated in static buffer
   */
  analyzeVitals(vitals: Record<string, number>): VitalsAnalysis {
    if (!this.isReady()) {
      throw new Error('WASM module not initialized');
    }

    const vitalsJson = JSON.stringify(vitals);
    const vitalsPtr = this.stringToPtr(vitalsJson);

    try {
      const resultPtr = this.wasmModule!.exports.wasm_analyze_vitals(vitalsPtr);
      const resultJson = this.ptrToString(resultPtr);
      
      this.wasmModule!.exports.wasm_free(resultPtr);
      
      const result = JSON.parse(resultJson);
      this.validateVitalsAnalysis(result);
      return result;

    } finally {
      this.wasmModule!.exports.free(vitalsPtr);
    }
  }

  /**
   * Simulate one step of training scenario
   * 
   * Contract: Pure function - same input always produces same output
   */
  simulateScenarioStep(scenario: Record<string, any>): ScenarioStep {
    if (!this.isReady()) {
      throw new Error('WASM module not initialized');
    }

    const scenarioJson = JSON.stringify(scenario);
    const scenarioPtr = this.stringToPtr(scenarioJson);

    try {
      const resultPtr = this.wasmModule!.exports.wasm_simulate_scenario_step(scenarioPtr);
      const resultJson = this.ptrToString(resultPtr);
      
      this.wasmModule!.exports.wasm_free(resultPtr);
      
      const result = JSON.parse(resultJson);
      this.validateScenarioStep(result);
      return result;

    } finally {
      this.wasmModule!.exports.free(scenarioPtr);
    }
  }

  /**
   * Echo test - verify string passing works correctly
   * 
   * Useful for debugging memory boundary issues
   */
  echo(input: string): string {
    if (!this.isReady()) {
      throw new Error('WASM module not initialized');
    }

    const inputPtr = this.stringToPtr(input);

    try {
      const resultPtr = this.wasmModule!.exports.wasm_echo(inputPtr);
      const resultJson = this.ptrToString(resultPtr);
      
      this.wasmModule!.exports.wasm_free(resultPtr);
      
      const result = JSON.parse(resultJson);
      return result.echo;

    } finally {
      this.wasmModule!.exports.free(inputPtr);
    }
  }

  /**
   * Cleanup: Destroy WASM engine
   * 
   * Call when done with WASM (e.g., on app shutdown)
   */
  destroy(): void {
    if (this.isReady()) {
      this.wasmModule!.exports.wasm_destroy_engine();
      this.isInitialized = false;
      this.wasmModule = null;
    }
  }

  // =========================================================================
  // Private: Memory Management
  // =========================================================================

  /**
   * Convert JavaScript string to WASM memory pointer
   * 
   * Encodes string as UTF-8, allocates in WASM heap, returns pointer
   */
  private stringToPtr(str: string): number {
    if (!this.wasmModule) {
      throw new Error('WASM module not available');
    }

    const encoder = new TextEncoder();
    const data = encoder.encode(str + '\0'); // Add null terminator

    if (data.length > this.MAX_RESULT_SIZE) {
      throw new Error(`Input string too large: ${data.length} > ${this.MAX_RESULT_SIZE}`);
    }

    const ptr = this.wasmModule.exports.malloc(data.length);

    // Write to WASM linear memory
    const memory = new Uint8Array(this.wasmModule.exports.memory.buffer);
    memory.set(data, ptr);

    return ptr;
  }

  /**
   * Convert WASM memory pointer to JavaScript string
   * 
   * Reads until null terminator, decodes UTF-8
   */
  private ptrToString(ptr: number): string {
    if (!this.wasmModule) {
      throw new Error('WASM module not available');
    }

    const memory = new Uint8Array(this.wasmModule.exports.memory.buffer);

    // Find null terminator
    let length = 0;
    while (
      ptr + length < memory.length &&
      memory[ptr + length] !== 0 &&
      length < this.MAX_RESULT_SIZE
    ) {
      length++;
    }

    if (length >= this.MAX_RESULT_SIZE) {
      throw new Error('Result buffer overflow detected');
    }

    // Convert bytes to string
    const bytes = memory.subarray(ptr, ptr + length);
    return new TextDecoder().decode(bytes);
  }

  // =========================================================================
  // Private: Validation
  // =========================================================================

  /**
   * Validate inference result JSON schema
   */
  private validateInferenceResult(result: any): void {
    if (!result.response) {
      throw new Error('Invalid inference result: missing response');
    }
    if (typeof result.confidence !== 'number') {
      throw new Error('Invalid inference result: confidence not a number');
    }
    if (result.confidence < 0 || result.confidence > 1) {
      throw new Error('Invalid inference result: confidence out of range [0, 1]');
    }
  }

  /**
   * Validate vitals analysis JSON schema
   */
  private validateVitalsAnalysis(result: any): void {
    const validSeverities = ['info', 'warning', 'critical'];
    if (!validSeverities.includes(result.severity)) {
      throw new Error(`Invalid severity: ${result.severity}`);
    }
    if (!Array.isArray(result.alerts)) {
      throw new Error('Invalid vitals analysis: alerts not an array');
    }
    if (!Array.isArray(result.recommendations)) {
      throw new Error('Invalid vitals analysis: recommendations not an array');
    }
  }

  /**
   * Validate scenario step JSON schema
   */
  private validateScenarioStep(result: any): void {
    if (!result.scenario_id) {
      throw new Error('Invalid scenario step: missing scenario_id');
    }
    if (!Array.isArray(result.events)) {
      throw new Error('Invalid scenario step: events not an array');
    }
    if (result.immutable !== true) {
      throw new Error('Invalid scenario step: immutable flag not set');
    }
  }
}

// ============================================================================
// Singleton Instance
// ============================================================================

let wasmInstance: WasmBridge | null = null;

export async function initializeWasm(wasmUrl: string = '/dist/wasm/optic-trigeminal.wasm'): Promise<WasmBridge> {
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
