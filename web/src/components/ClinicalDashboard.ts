import { Component } from './component';
import type { Patient, Role, RoleCapabilities, TrainingScenario } from '@api/types';
import { getWasmBridge } from '@api/wasm-bridge';
import { apiClient } from '@api/client';
import { showToast } from '@utils/ui-helpers';
import { Modal, type ModalConfig } from './Modal';

export interface ClinicalDashboardConfig {
  currentStaffName: string;
  currentRole: Role;
  roleCapabilities: RoleCapabilities;
  patients: Patient[];
  trainingScenarios: TrainingScenario[];
  acmkSessionId: string | null;
  acmkMode: 'simulation' | 'real_world' | null;
  onSignOut: () => void;
  onSelectPatient: (patientId: number) => void;
  onStartTraining: () => void;
  onOpenReasoningTrace: () => void;
  // Admit/discharge/assign all change the server-side roster and staff
  // assignments -- the dashboard re-fetches after any of them, but the
  // global store (main-refactored.ts) owns that fetch and the patient list
  // other screens (PatientDetail) read from, so this asks the parent to
  // redo it rather than the dashboard quietly diverging from the store.
  onPatientsChanged: () => void;
}

interface DerivedAlert {
  patientId: number;
  patientName: string;
  severity: 'watch' | 'action_required' | 'escalation_suggested';
  reason: string;
}

// Shared dashboard for RN / Charge Nurse / Provider -- one clinical view,
// differentiated by roleCapabilities rather than three near-duplicate
// components, since the underlying patient data and workflows are the same
// and only which actions are available (chart vs. review-only, unit-wide
// overview vs. own assignment) differs by role.
export class ClinicalDashboard extends Component {
  private config: ClinicalDashboardConfig;
  private alerts: DerivedAlert[] = [];
  private acknowledged: Set<string> = new Set();
  private activeModal: Modal | null = null;

  constructor(config: ClinicalDashboardConfig) {
    super();
    this.config = config;
    this.alerts = this.deriveAlerts(config.patients);
  }

  // Real threshold-based rules over real vitals data -- not an AI claim,
  // just the kind of "SpO2 below 92%" bedside logic every unit already
  // runs on. Deliberately simple and inspectable.
  private deriveAlerts(patients: Patient[]): DerivedAlert[] {
    const alerts: DerivedAlert[] = [];
    for (const p of patients) {
      const v = p.vitals;
      if (v.spo2 < 90) {
        alerts.push({ patientId: p.id, patientName: p.name, severity: 'escalation_suggested', reason: `SpO2 critically low at ${v.spo2}%` });
      } else if (v.spo2 < 94) {
        alerts.push({ patientId: p.id, patientName: p.name, severity: 'action_required', reason: `SpO2 low at ${v.spo2}%` });
      }
      if (v.hr > 130 || v.hr < 45) {
        alerts.push({ patientId: p.id, patientName: p.name, severity: 'escalation_suggested', reason: `Heart rate out of range at ${v.hr} bpm` });
      } else if (v.hr > 110) {
        alerts.push({ patientId: p.id, patientName: p.name, severity: 'watch', reason: `Heart rate trending high at ${v.hr} bpm` });
      }
      if (v.temp >= 39.0) {
        alerts.push({ patientId: p.id, patientName: p.name, severity: 'action_required', reason: `Fever at ${v.temp.toFixed(1)}°C` });
      }
      if (v.bp_sys < 90) {
        alerts.push({ patientId: p.id, patientName: p.name, severity: 'action_required', reason: `Hypotensive at ${v.bp_sys}/${v.bp_dia} mmHg` });
      }
    }
    // Highest severity first
    const order = { escalation_suggested: 0, action_required: 1, watch: 2 };
    return alerts.sort((a, b) => order[a.severity] - order[b.severity]);
  }

  render(): string {
    const caps = this.config.roleCapabilities;
    const roleLabel = caps.displayName;

    return `
      <div class="min-h-screen bg-slate-950 text-slate-100 p-6 relative overflow-hidden">
        <div class="fixed top-[-10%] left-[-10%] w-[40%] h-[40%] rounded-full bg-cyan-600/10 blur-[120px] pointer-events-none"></div>
        <div class="fixed bottom-[-10%] right-[-10%] w-[40%] h-[40%] rounded-full bg-blue-600/10 blur-[120px] pointer-events-none"></div>

        <div class="max-w-7xl mx-auto relative z-10 animate-fade-in-up space-y-6">
          ${this.renderHeader(roleLabel)}
          ${this.config.acmkMode === 'simulation' ? this.renderSimulationBanner() : ''}
          ${caps.canViewAllPatients ? this.renderUnitOverview() : ''}

          <div class="grid grid-cols-1 lg:grid-cols-3 gap-8">
            <div class="lg:col-span-2 space-y-6">
              <div id="alerts-panel">${this.renderAlertsPanel()}</div>
              ${this.renderPatientSection()}
            </div>

            <div class="space-y-6">
              ${caps.canAccessAdmin || caps.canChartActions || true ? this.renderTrainingPanel() : ''}
              ${this.renderEdgeDiagnostics()}
            </div>
          </div>
        </div>
      </div>
    `;
  }

  private renderHeader(roleLabel: string): string {
    return `
      <header class="glass-panel rounded-2xl p-6 flex justify-between items-center">
        <div class="flex items-center gap-4">
          <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-cyan-500 to-blue-600 flex items-center justify-center shadow-lg shadow-cyan-500/20">
            <svg class="w-6 h-6 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19.428 15.428a2 2 0 00-1.022-.547l-2.387-.477a6 6 0 00-3.86.517l-.318.158a6 6 0 01-3.86.517L6.05 15.21a2 2 0 00-1.806.547M8 4h8l-1 1v5.172a2 2 0 00.586 1.414l5 5c1.26 1.26.367 3.414-1.415 3.414H4.828c-1.782 0-2.674-2.154-1.414-3.414l5-5A2 2 0 009 10.172V5L8 4z"></path>
            </svg>
          </div>
          <div>
            <h1 class="text-2xl font-bold text-white tracking-tight">OpticTrigeminal <span class="text-xs align-top bg-cyan-500/20 text-cyan-400 px-2 py-0.5 rounded-full ml-1">Edge V3</span></h1>
            <p class="text-slate-400 text-sm">${roleLabel} Dashboard</p>
          </div>
        </div>

        <div class="flex items-center gap-6">
          <div class="text-right">
            <div class="text-sm font-semibold text-white">${this.config.currentStaffName}</div>
            <div class="text-xs text-cyan-400 font-medium uppercase tracking-wider">${roleLabel}</div>
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

  private renderSimulationBanner(): string {
    return `
      <div id="simulation-banner" class="rounded-xl border border-amber-500/40 bg-amber-500/10 px-5 py-3 flex items-center justify-between gap-4">
        <div class="flex items-center gap-3">
          <span class="text-lg">⚠️</span>
          <div class="text-sm">
            <span class="font-semibold text-amber-300">SIMULATION MODE — no real-world effect.</span>
            <span class="text-amber-200/70 ml-2">Session <code class="text-amber-200/90">${(this.config.acmkSessionId || '').substring(0, 8)}…</code> is sandboxed; nothing here reaches a real patient record.</span>
          </div>
        </div>
        <button id="btn-dismiss-banner" class="text-amber-300/60 hover:text-amber-200 text-lg leading-none">&times;</button>
      </div>
    `;
  }

  private renderUnitOverview(): string {
    const total = this.config.patients.length;
    const critical = this.config.patients.filter(p => p.acuity_score >= 7).length;
    const warning = this.config.patients.filter(p => p.acuity_score >= 4 && p.acuity_score < 7).length;
    const stable = total - critical - warning;

    const stat = (label: string, value: number, color: string) => `
      <div class="glass-card p-4 flex flex-col items-center justify-center">
        <div class="text-2xl font-bold ${color}">${value}</div>
        <div class="text-xs text-slate-400 uppercase tracking-wider mt-1">${label}</div>
      </div>
    `;

    return `
      <div class="grid grid-cols-4 gap-4">
        ${stat('Total', total, 'text-white')}
        ${stat('Critical', critical, 'text-red-400')}
        ${stat('Warning', warning, 'text-amber-400')}
        ${stat('Stable', stable, 'text-green-400')}
      </div>
    `;
  }

  private renderAlertsPanel(): string {
    const active = this.alerts.filter(a => !this.acknowledged.has(`${a.patientId}:${a.reason}`));

    if (active.length === 0) {
      return `
        <div class="glass-card p-5 flex items-center gap-3">
          <span class="text-lg">✓</span>
          <span class="text-sm text-slate-400">No active vitals alerts across your patients.</span>
        </div>
      `;
    }

    const severityStyle: Record<DerivedAlert['severity'], { label: string; classes: string; icon: string }> = {
      escalation_suggested: { label: 'Escalation Suggested', classes: 'border-red-500/40 bg-red-500/10 text-red-300', icon: '🚨' },
      action_required: { label: 'Action Required', classes: 'border-orange-500/40 bg-orange-500/10 text-orange-300', icon: '⚠️' },
      watch: { label: 'Watch', classes: 'border-amber-500/30 bg-amber-500/10 text-amber-300', icon: '👁️' }
    };

    return `
      <div class="glass-card p-5">
        <h2 class="text-lg font-semibold text-white mb-4 flex items-center gap-2">
          Clinical Alerts
          <span class="text-xs font-normal text-slate-500">(threshold-based, derived from live vitals)</span>
        </h2>
        <div class="space-y-2">
          ${active.map(a => {
            const s = severityStyle[a.severity];
            const key = `${a.patientId}:${a.reason}`;
            return `
              <div class="flex items-center justify-between gap-3 rounded-lg border px-4 py-3 ${s.classes}">
                <div class="flex items-center gap-3 min-w-0">
                  <span>${s.icon}</span>
                  <div class="min-w-0">
                    <div class="text-xs font-semibold uppercase tracking-wider opacity-80">${s.label}</div>
                    <div class="text-sm text-slate-100 truncate">${a.patientName}: ${a.reason}</div>
                  </div>
                </div>
                <div class="flex items-center gap-2 shrink-0">
                  ${this.config.roleCapabilities.canOverrideAlerts ? `
                    <button class="btn-override-alert text-xs font-medium px-3 py-1.5 rounded-md bg-slate-900/50 border border-amber-500/30 text-amber-300 hover:bg-amber-500/10 transition-colors" data-patient-id="${a.patientId}" data-reason="${this.escapeAttr(a.reason)}" data-patient-name="${this.escapeAttr(a.patientName)}">
                      Override
                    </button>
                  ` : ''}
                  <button class="btn-ack-alert text-xs font-medium px-3 py-1.5 rounded-md bg-slate-900/50 border border-white/10 hover:bg-slate-900/80 transition-colors" data-key="${key}" data-patient-id="${a.patientId}" data-reason="${this.escapeAttr(a.reason)}">
                    Acknowledge
                  </button>
                </div>
              </div>
            `;
          }).join('')}
        </div>
      </div>
    `;
  }

  private escapeAttr(s: string): string {
    return s.replace(/&/g, '&amp;').replace(/"/g, '&quot;');
  }

  private renderPatientSection(): string {
    const caps = this.config.roleCapabilities;
    return `
      <div>
        <div class="flex justify-between items-end mb-4">
          <h2 class="text-xl font-semibold text-white flex items-center gap-2">
            <svg class="w-5 h-5 text-cyan-500" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z"></path>
            </svg>
            ${caps.canViewAllPatients ? 'All Patients' : 'Your Patients'}
          </h2>
          <div class="flex items-center gap-3">
            <span class="text-sm text-slate-400">Total: ${this.config.patients.length}</span>
            ${caps.canAdmitPatient ? `
              <button id="btn-admit-patient" class="text-xs font-medium px-3 py-1.5 rounded-md bg-cyan-500/10 border border-cyan-500/30 text-cyan-300 hover:bg-cyan-500/20 transition-colors">
                + Admit Patient
              </button>
            ` : ''}
          </div>
        </div>
        <div id="patient-grid" class="grid grid-cols-1 md:grid-cols-2 gap-4">
          ${this.renderPatientCards()}
        </div>
      </div>
    `;
  }

  private renderPatientCards(): string {
    const caps = this.config.roleCapabilities;
    const canChart = caps.canChartActions;

    return this.config.patients.map(patient => {
      const isCritical = patient.acuity_score >= 7;
      const isWarning = patient.acuity_score >= 4 && patient.acuity_score < 7;

      let statusColor = 'text-green-400';
      let statusBg = 'bg-green-400/10 border-green-400/20';
      let pulseEffect = '';

      if (isCritical) {
        statusColor = 'text-red-400';
        statusBg = 'bg-red-400/10 border-red-400/30';
        pulseEffect = 'animate-pulse-slow shadow-lg shadow-red-500/10 border-red-500/50';
      } else if (isWarning) {
        statusColor = 'text-amber-400';
        statusBg = 'bg-amber-400/10 border-amber-400/30';
      }

      return `
        <div class="glass-card p-5 cursor-pointer group patient-card ${pulseEffect}" data-patient-id="${patient.id}">
          <div class="flex justify-between items-start mb-4">
            <div>
              <h3 class="font-bold text-lg text-white group-hover:text-cyan-400 transition-colors">${patient.name}</h3>
              <div class="text-xs text-slate-400 mt-1 flex items-center gap-2">
                <span class="inline-flex items-center justify-center bg-slate-800 rounded px-2 py-0.5 border border-slate-700">Room ${patient.room}</span>
                <span>${patient.mrn}</span>
              </div>
            </div>
            <div class="px-2 py-1 rounded text-xs font-semibold border ${statusBg} ${statusColor}">
              Score: ${patient.acuity_score}
            </div>
          </div>

          <div class="grid grid-cols-4 gap-2 mb-3">
            <div class="bg-slate-900/50 p-2 rounded-lg border border-slate-800 flex flex-col items-center justify-center">
              <div class="text-[10px] text-slate-500 font-medium uppercase tracking-wider mb-1">HR</div>
              <div class="font-bold text-sm ${patient.vitals.hr > 100 ? 'text-red-400' : 'text-slate-200'}">${patient.vitals.hr}</div>
            </div>
            <div class="bg-slate-900/50 p-2 rounded-lg border border-slate-800 flex flex-col items-center justify-center">
              <div class="text-[10px] text-slate-500 font-medium uppercase tracking-wider mb-1">SpO2</div>
              <div class="font-bold text-sm ${patient.vitals.spo2 < 95 ? 'text-red-400' : 'text-cyan-400'}">${patient.vitals.spo2}%</div>
            </div>
            <div class="bg-slate-900/50 p-2 rounded-lg border border-slate-800 flex flex-col items-center justify-center">
              <div class="text-[10px] text-slate-500 font-medium uppercase tracking-wider mb-1">RR</div>
              <div class="font-bold text-sm text-slate-200">${patient.vitals.rr}</div>
            </div>
            <div class="bg-slate-900/50 p-2 rounded-lg border border-slate-800 flex flex-col items-center justify-center">
              <div class="text-[10px] text-slate-500 font-medium uppercase tracking-wider mb-1">BP</div>
              <div class="font-bold text-[11px] text-slate-300">${patient.vitals.bp_sys}/${patient.vitals.bp_dia}</div>
            </div>
          </div>

          <div class="flex items-center justify-between gap-2">
            <div class="text-xs text-cyan-400/80 group-hover:text-cyan-300 transition-colors">
              ${canChart ? 'View & chart →' : 'View details →'}
            </div>
            ${(caps.canAssignPatients || caps.canDischargePatient) ? `
              <div class="flex items-center gap-1.5">
                ${caps.canAssignPatients ? `
                  <button class="btn-assign-patient text-[11px] font-medium px-2 py-1 rounded border border-white/10 text-slate-300 hover:bg-slate-900/60 transition-colors" data-patient-id="${patient.id}">
                    Assign
                  </button>
                ` : ''}
                ${caps.canDischargePatient ? `
                  <button class="btn-discharge-patient text-[11px] font-medium px-2 py-1 rounded border border-red-500/20 text-red-300 hover:bg-red-500/10 transition-colors" data-patient-id="${patient.id}" data-patient-name="${this.escapeAttr(patient.name)}">
                    Discharge
                  </button>
                ` : ''}
              </div>
            ` : ''}
          </div>
        </div>
      `;
    }).join('');
  }

  private renderTrainingPanel(): string {
    return `
      <div class="glass-card p-6">
        <h2 class="text-lg font-semibold text-white mb-2 flex items-center gap-2">
          <svg class="w-5 h-5 text-purple-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19.428 15.428a2 2 0 00-1.022-.547l-2.387-.477a6 6 0 00-3.86.517l-.318.158a6 6 0 01-3.86.517L6.05 15.21a2 2 0 00-1.806.547M8 4h8l-1 1v5.172a2 2 0 00.586 1.414l5 5c1.26 1.26.367 3.414-1.415 3.414H4.828c-1.782 0-2.674-2.154-1.414-3.414l5-5A2 2 0 009 10.172V5L8 4z"></path>
          </svg>
          Clinical Simulation
        </h2>
        <p class="text-sm text-slate-400 mb-6">Enter the WASM-powered AI clinical simulation environment.</p>
        <button id="btn-start-training" class="btn-neon w-full py-3 flex justify-center items-center gap-2">
          <span>Enter Training Mode</span>
          <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M14 5l7 7m0 0l-7 7m7-7H3"></path>
          </svg>
        </button>
        <button id="btn-open-reasoning" class="btn-outline-neon w-full py-2.5 mt-3 flex justify-center items-center gap-2 text-sm">
          <span>View Reasoning Trace</span>
          <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9.663 17h4.673M12 3v1m6.364 1.636l-.707.707M21 12h-1M4 12H3m3.343-5.657l-.707-.707m2.828 9.9a5 5 0 117.072 0l-.548.547A3.374 3.374 0 0014 18.469V19a2 2 0 11-4 0v-.531c0-.895-.356-1.754-.988-2.386l-.548-.547z"></path>
          </svg>
        </button>
      </div>
    `;
  }

  private renderEdgeDiagnostics(): string {
    return `
      <div class="glass-card p-6 border-cyan-900/50">
        <h2 class="text-lg font-semibold text-white mb-2 flex items-center gap-2">
          <svg class="w-5 h-5 text-cyan-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 3v2m6-2v2M9 19v2m6-2v2M5 9H3m2 6H3m14-6h2m-2 6h2M7 19h10a2 2 0 002-2V7a2 2 0 00-2-2H7a2 2 0 00-2 2v10a2 2 0 002 2zM9 9h6v6H9V9z"></path>
          </svg>
          Edge Diagnostics
        </h2>
        <p class="text-sm text-slate-400 mb-6">Test native browser inference powered by WebAssembly.</p>

        <div class="flex flex-col gap-3">
          <button id="btn-test-wasm-infer" class="btn-outline-neon py-2 text-sm flex justify-center items-center gap-2">
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 10h.01M12 10h.01M16 10h.01M9 16H5a2 2 0 01-2-2V6a2 2 0 012-2h14a2 2 0 012 2v8a2 2 0 01-2 2h-5l-5 5v-5z"></path></svg>
            Test Text Inference
          </button>
          <button id="btn-test-wasm-vitals" class="btn-outline-neon py-2 text-sm flex justify-center items-center gap-2">
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4.318 6.318a4.5 4.5 0 000 6.364L12 20.364l7.682-7.682a4.5 4.5 0 00-6.364-6.364L12 7.636l-1.318-1.318a4.5 4.5 0 00-6.364 0z"></path></svg>
            Test Vitals Analysis
          </button>
        </div>

        <div id="wasm-test-output" class="mt-4 p-3 bg-slate-950/50 border border-slate-800 rounded font-mono text-xs text-cyan-300 hidden overflow-auto max-h-40 break-all shadow-inner"></div>
      </div>
    `;
  }

  mount(container: HTMLElement): void {
    container.innerHTML = this.render();
    this.element = container;
    this.setupEventListeners();
  }

  unmount(): void {
    this.closeModal();
    this.element = null;
  }

  private setupEventListeners(): void {
    this.on('#btn-signout', 'click', () => this.config.onSignOut());

    this.on('.patient-card', 'click', (e) => {
      const card = (e.target as HTMLElement).closest('.patient-card') as HTMLElement;
      const patientId = parseInt(card.getAttribute('data-patient-id')!);
      this.config.onSelectPatient(patientId);
    });

    this.setupUnitManagementListeners();

    this.on('#btn-start-training', 'click', () => this.config.onStartTraining());
    this.on('#btn-open-reasoning', 'click', () => this.config.onOpenReasoningTrace());

    this.on('#btn-dismiss-banner', 'click', () => {
      const banner = this.querySelector('#simulation-banner');
      banner?.remove();
    });

    this.attachAlertListeners();

    this.on('#btn-test-wasm-infer', 'click', async () => {
      const btn = this.element?.querySelector('#btn-test-wasm-infer') as HTMLButtonElement;
      if (btn) btn.disabled = true;
      this.showWasmOutput('Processing inference in WASM Worker...');
      try {
        const wasm = getWasmBridge();
        const result = await wasm.infer('What are the early signs of sepsis?', 128);
        this.showWasmOutput(JSON.stringify(result, null, 2));
      } catch (e: any) {
        this.showWasmOutput(`Error: ${e.message}`);
      } finally {
        if (btn) btn.disabled = false;
      }
    });

    this.on('#btn-test-wasm-vitals', 'click', async () => {
      const btn = this.element?.querySelector('#btn-test-wasm-vitals') as HTMLButtonElement;
      if (btn) btn.disabled = true;
      this.showWasmOutput('Analyzing vitals in WASM Worker...');
      try {
        const wasm = getWasmBridge();
        const result = await wasm.analyzeVitals({
          heart_rate: 130,
          blood_pressure: 85,
          spo2: 91,
          temperature: 39.1,
          respiratory_rate: 28
        });
        this.showWasmOutput(JSON.stringify(result, null, 2));
      } catch (e: any) {
        this.showWasmOutput(`Error: ${e.message}`);
      } finally {
        if (btn) btn.disabled = false;
      }
    });
  }

  private showWasmOutput(text: string): void {
    const el = this.element?.querySelector('#wasm-test-output');
    if (el) {
      el.classList.remove('hidden');
      el.textContent = text;
    }
  }

  // Binds the acknowledge buttons currently in #alerts-panel. Called at
  // initial mount and again after every acknowledge, since Component.on()
  // binds listeners directly to the elements present at call time rather
  // than delegating -- replacing #alerts-panel's innerHTML (see below)
  // creates fresh buttons with no listeners unless this re-runs on them.
  private attachAlertListeners(): void {
    this.on('.btn-ack-alert', 'click', async (e) => {
      const btn = e.currentTarget as HTMLButtonElement;
      const key = btn.dataset.key!;
      const patientId = btn.dataset.patientId!;
      const reason = btn.dataset.reason || '';
      btn.disabled = true;
      btn.textContent = '...';
      try {
        await apiClient.recordHumanEvent('acknowledge', `patient:${patientId}`, reason, this.config.acmkSessionId || undefined);
        this.acknowledged.add(key);
        // Re-render just the alerts panel, not the whole dashboard --
        // this.mount(this.element!) here used to tear down and rebuild
        // every DOM node on screen (header, banner, patient grid,
        // everything), replaying the .animate-fade-in-up entrance
        // animation on the whole page for a one-line state change.
        const panel = this.element?.querySelector('#alerts-panel');
        if (panel) panel.innerHTML = this.renderAlertsPanel();
        this.attachAlertListeners();
        showToast('Alert acknowledged', 'success');
      } catch (err: any) {
        btn.disabled = false;
        btn.textContent = 'Acknowledge';
        showToast(`Failed to acknowledge: ${err.message}`, 'error');
      }
    });

    this.on('.btn-override-alert', 'click', (e) => {
      const btn = e.currentTarget as HTMLButtonElement;
      const patientId = parseInt(btn.dataset.patientId!);
      const alertDescription = btn.dataset.reason || '';
      const patientName = btn.dataset.patientName || '';

      this.openModal({
        title: `Override Alert: ${patientName}`,
        submitLabel: 'Confirm Override',
        fields: [
          { id: 'reason', label: `Clinical justification for overriding "${alertDescription}"`, type: 'textarea', placeholder: 'e.g. Chronic baseline, provider aware, monitoring per orders', required: true }
        ],
        onCancel: () => this.closeModal(),
        onSubmit: async (values) => {
          if (!values.reason) {
            this.activeModal?.showError('A reason is required.');
            return;
          }
          try {
            const result = await apiClient.overrideAlert(patientId, alertDescription, values.reason, this.config.acmkSessionId || undefined);
            const key = `${patientId}:${alertDescription}`;
            this.acknowledged.add(key);
            const panel = this.element?.querySelector('#alerts-panel');
            if (panel) panel.innerHTML = this.renderAlertsPanel();
            this.attachAlertListeners();
            this.closeModal();
            showToast(`Alert overridden by ${result.overridden_by}`, 'success');
          } catch (err: any) {
            this.activeModal?.showError(err.message || 'Override failed');
          }
        }
      });
    });
  }

  // === Admit / Discharge / Assign (Charge Nurse / Provider) ===============

  private openModal(config: ModalConfig): void {
    const root = document.getElementById('modal-root');
    if (!root) return;
    this.activeModal = new Modal(config);
    this.activeModal.mount(root);
  }

  private closeModal(): void {
    this.activeModal?.unmount();
    this.activeModal = null;
  }

  private setupUnitManagementListeners(): void {
    this.on('#btn-admit-patient', 'click', () => {
      this.openModal({
        title: 'Admit Patient',
        submitLabel: 'Admit',
        fields: [
          { id: 'name', label: 'Patient Name (Last, First)', type: 'text', placeholder: 'Smith, John', required: true },
          { id: 'mrn', label: 'MRN', type: 'text', placeholder: '99823400', required: true },
          { id: 'room', label: 'Room', type: 'text', placeholder: '412-A', required: true },
          { id: 'diagnosis', label: 'Admission Diagnosis', type: 'text', placeholder: 'Pneumonia', required: true },
          {
            id: 'acuity_score', label: 'Acuity Score', type: 'select', required: true, defaultValue: '3',
            options: Array.from({ length: 10 }, (_, i) => ({ value: String(i + 1), label: String(i + 1) }))
          }
        ],
        onCancel: () => this.closeModal(),
        onSubmit: async (values) => {
          if (!values.name || !values.mrn || !values.room || !values.diagnosis) {
            this.activeModal?.showError('All fields are required.');
            return;
          }
          try {
            await apiClient.admitPatient(values.name, values.mrn, values.room, values.diagnosis, parseInt(values.acuity_score) || 3);
            this.closeModal();
            showToast(`${values.name} admitted`, 'success');
            this.config.onPatientsChanged();
          } catch (err: any) {
            this.activeModal?.showError(err.message || 'Admission failed');
          }
        }
      });
    });

    this.on('.btn-discharge-patient', 'click', (e) => {
      e.stopPropagation();
      const btn = e.currentTarget as HTMLButtonElement;
      const patientId = parseInt(btn.dataset.patientId!);
      const patientName = btn.dataset.patientName || 'this patient';

      this.openModal({
        title: `Discharge ${patientName}`,
        submitLabel: 'Discharge',
        fields: [
          { id: 'reason', label: 'Discharge Reason / Disposition', type: 'textarea', placeholder: 'e.g. Discharged home, stable, follow-up in 1 week', required: true }
        ],
        onCancel: () => this.closeModal(),
        onSubmit: async (values) => {
          if (!values.reason) {
            this.activeModal?.showError('A discharge reason is required.');
            return;
          }
          try {
            await apiClient.dischargePatient(patientId, values.reason);
            this.closeModal();
            showToast(`${patientName} discharged`, 'success');
            this.config.onPatientsChanged();
          } catch (err: any) {
            this.activeModal?.showError(err.message || 'Discharge failed');
          }
        }
      });
    });

    this.on('.btn-assign-patient', 'click', async (e) => {
      e.stopPropagation();
      const btn = e.currentTarget as HTMLButtonElement;
      const patientId = parseInt(btn.dataset.patientId!);

      let staff: { staff_id: string; name: string; role: Role }[] = [];
      try {
        staff = await apiClient.listStaff();
      } catch (err: any) {
        showToast(`Failed to load staff list: ${err.message}`, 'error');
        return;
      }
      const nursingStaff = staff.filter(s => s.role === 'rn' || s.role === 'charge_nurse');
      if (nursingStaff.length === 0) {
        showToast('No nursing staff available to assign', 'error');
        return;
      }

      this.openModal({
        title: 'Assign Patient',
        submitLabel: 'Assign',
        fields: [
          {
            id: 'staff_id', label: 'Assign to', type: 'select', required: true,
            options: nursingStaff.map(s => ({ value: s.staff_id, label: `${s.name} (${s.role === 'charge_nurse' ? 'Charge Nurse' : 'RN'})` }))
          }
        ],
        onCancel: () => this.closeModal(),
        onSubmit: async (values) => {
          if (!values.staff_id) {
            this.activeModal?.showError('Select a staff member.');
            return;
          }
          try {
            await apiClient.assignPatient(values.staff_id, patientId);
            this.closeModal();
            showToast('Patient assigned', 'success');
            this.config.onPatientsChanged();
          } catch (err: any) {
            this.activeModal?.showError(err.message || 'Assignment failed');
          }
        }
      });
    });
  }
}
