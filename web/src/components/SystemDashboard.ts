import { Component } from './component';
import { apiClient } from '@api/client';
import { getWasmBridge } from '@api/wasm-bridge';

export interface SystemDashboardConfig {
  currentStaffName: string;
  onSignOut: () => void;
}

// IT/System role: infrastructure health only. Deliberately shows zero
// patient data (matches roleCapabilities.canViewVitals === false in
// main-refactored.ts) -- IT staff troubleshoot the system, not the census.
export class SystemDashboard extends Component {
  private config: SystemDashboardConfig;
  private backendHealthy: boolean | null = null;
  private wasmReady: boolean | null = null;

  constructor(config: SystemDashboardConfig) {
    super();
    this.config = config;
  }

  render(): string {
    return `
      <div class="min-h-screen bg-slate-950 text-slate-100 p-6 relative overflow-hidden">
        <div class="fixed top-[-10%] left-[-10%] w-[40%] h-[40%] rounded-full bg-emerald-600/10 blur-[120px] pointer-events-none"></div>
        <div class="fixed bottom-[-10%] right-[-10%] w-[40%] h-[40%] rounded-full bg-cyan-600/10 blur-[120px] pointer-events-none"></div>

        <div class="max-w-5xl mx-auto relative z-10 animate-fade-in-up space-y-6">
          ${this.renderHeader()}
          ${this.renderStatusGrid()}
          ${this.renderConfigPanel()}
          ${this.renderWasmDiagnostics()}
        </div>
      </div>
    `;
  }

  private renderHeader(): string {
    return `
      <header class="glass-panel rounded-2xl p-6 flex justify-between items-center">
        <div class="flex items-center gap-4">
          <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-emerald-500 to-teal-600 flex items-center justify-center shadow-lg shadow-emerald-500/20">
            <svg class="w-6 h-6 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9.75 17L9 20l-1 1h8l-1-1-.75-3M3 13h18M5 17h14a2 2 0 002-2V5a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z"></path>
            </svg>
          </div>
          <div>
            <h1 class="text-2xl font-bold text-white tracking-tight">OpticTrigeminal <span class="text-xs align-top bg-emerald-500/20 text-emerald-300 px-2 py-0.5 rounded-full ml-1">System</span></h1>
            <p class="text-slate-400 text-sm">Infrastructure health — no clinical data on this view</p>
          </div>
        </div>
        <div class="flex items-center gap-6">
          <div class="text-right">
            <div class="text-sm font-semibold text-white">${this.config.currentStaffName}</div>
            <div class="text-xs text-emerald-300 font-medium uppercase tracking-wider">IT/System</div>
          </div>
          <button id="btn-signout" class="btn-outline-neon px-4 py-2 text-sm flex items-center gap-2">
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 16l4-4m0 0l-4-4m4 4H7m6 4v1a3 3 0 01-3 3H6a3 3 0 01-3-3V7a3 3 0 013-3h4a3 3 0 013 3v1"></path>
            </svg>
            Sign Out
          </button>
        </div>
      </header>
    `;
  }

  private renderStatusGrid(): string {
    const statusPill = (label: string, ok: boolean | null) => {
      const color = ok === null ? 'text-slate-400 bg-slate-800/50 border-slate-700' :
                     ok ? 'text-emerald-300 bg-emerald-500/10 border-emerald-500/30' :
                     'text-red-300 bg-red-500/10 border-red-500/30';
      const text = ok === null ? 'Checking…' : ok ? 'Online' : 'Unreachable';
      return `
        <div class="glass-card p-5">
          <div class="text-xs text-slate-500 uppercase tracking-wider mb-2">${label}</div>
          <div class="inline-flex items-center gap-2 px-3 py-1.5 rounded-lg border text-sm font-semibold ${color}">
            <span class="w-1.5 h-1.5 rounded-full ${ok === null ? 'bg-slate-500' : ok ? 'bg-emerald-400' : 'bg-red-400'}"></span>
            ${text}
          </div>
        </div>
      `;
    };

    return `
      <div id="status-grid" class="grid grid-cols-1 md:grid-cols-2 gap-4">
        ${statusPill('Backend (localhost:8080)', this.backendHealthy)}
        ${statusPill('WASM Edge Runtime', this.wasmReady)}
      </div>
    `;
  }

  private renderConfigPanel(): string {
    return `
      <div class="glass-card p-6">
        <h2 class="text-lg font-semibold text-white mb-4 flex items-center gap-2">
          <svg class="w-5 h-5 text-emerald-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z"></path>
          </svg>
          Server Configuration
        </h2>
        <p class="text-xs text-slate-500 mb-4">Read-only view of security-relevant runtime flags. Change these via environment variables and restart the server (see README.md).</p>
        <div class="grid grid-cols-1 sm:grid-cols-2 gap-3 text-sm">
          <div class="rounded-lg border border-slate-800 bg-slate-900/40 px-4 py-3">
            <div class="text-xs text-slate-500 uppercase tracking-wider">Real-world mode</div>
            <div class="text-slate-200 mt-1">Gated by <code>ACMK_ENABLE_REAL_WORLD</code> + elevated role. Defaults closed.</div>
          </div>
          <div class="rounded-lg border border-slate-800 bg-slate-900/40 px-4 py-3">
            <div class="text-xs text-slate-500 uppercase tracking-wider">Audit trail</div>
            <div class="text-slate-200 mt-1">Hash-chained, append-only. See ADMIN dashboard for live view.</div>
          </div>
          <div class="rounded-lg border border-slate-800 bg-slate-900/40 px-4 py-3">
            <div class="text-xs text-slate-500 uppercase tracking-wider">FHIR integration</div>
            <div class="text-slate-200 mt-1">Configured via <code>FHIR_BASE_URL</code>. Uses OAuth2 client-credentials, not yet the JWT-assertion flow production Epic requires.</div>
          </div>
          <div class="rounded-lg border border-slate-800 bg-slate-900/40 px-4 py-3">
            <div class="text-xs text-slate-500 uppercase tracking-wider">Sign-in lockout</div>
            <div class="text-slate-200 mt-1">5 failed attempts locks an account for 5 minutes.</div>
          </div>
        </div>
      </div>
    `;
  }

  private renderWasmDiagnostics(): string {
    return `
      <div class="glass-card p-6 border-emerald-900/50">
        <h2 class="text-lg font-semibold text-white mb-2 flex items-center gap-2">
          <svg class="w-5 h-5 text-emerald-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 3v2m6-2v2M9 19v2m6-2v2M5 9H3m2 6H3m14-6h2m-2 6h2M7 19h10a2 2 0 002-2V7a2 2 0 00-2-2H7a2 2 0 00-2 2v10a2 2 0 002 2zM9 9h6v6H9V9z"></path>
          </svg>
          Edge Diagnostics
        </h2>
        <p class="text-sm text-slate-400 mb-6">Round-trip test of the in-browser WASM inference module (no server call).</p>
        <button id="btn-test-wasm" class="btn-outline-neon py-2 px-4 text-sm">Run Diagnostic</button>
        <div id="wasm-test-output" class="mt-4 p-3 bg-slate-950/50 border border-slate-800 rounded font-mono text-xs text-emerald-300 hidden overflow-auto max-h-40 break-all shadow-inner"></div>
      </div>
    `;
  }

  mount(container: HTMLElement): void {
    container.innerHTML = this.render();
    this.element = container;
    this.setupEventListeners();
    this.checkStatus();
  }

  unmount(): void {
    this.element = null;
  }

  private setupEventListeners(): void {
    this.on('#btn-signout', 'click', () => this.config.onSignOut());
    this.on('#btn-test-wasm', 'click', async () => {
      const btn = this.element?.querySelector('#btn-test-wasm') as HTMLButtonElement;
      if (btn) btn.disabled = true;
      this.showWasmOutput('Running diagnostic...');
      try {
        const wasm = getWasmBridge();
        const result = await wasm.infer('System diagnostic ping', 32);
        this.showWasmOutput(JSON.stringify(result, null, 2));
      } catch (e: any) {
        this.showWasmOutput(`Error: ${e.message}`);
      } finally {
        if (btn) btn.disabled = false;
      }
    });
  }

  private async checkStatus(): Promise<void> {
    this.backendHealthy = await apiClient.testConnection();
    this.refreshStatusGrid();

    try {
      const wasm = getWasmBridge();
      this.wasmReady = wasm.isReady();
    } catch {
      this.wasmReady = false;
    }
    this.refreshStatusGrid();
  }

  private refreshStatusGrid(): void {
    const el = this.element?.querySelector('#status-grid');
    if (el) el.outerHTML = this.renderStatusGrid();
  }

  private showWasmOutput(text: string): void {
    const el = this.element?.querySelector('#wasm-test-output');
    if (el) {
      el.classList.remove('hidden');
      el.textContent = text;
    }
  }
}
