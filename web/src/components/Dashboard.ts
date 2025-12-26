import { Component } from './component';
import type { Patient, Role, RoleCapabilities, TrainingScenario } from '@api/types';

export interface DashboardConfig {
  currentStaffName: string;
  currentRole: Role;
  roleCapabilities: RoleCapabilities;
  patients: Patient[];
  trainingScenarios: TrainingScenario[];
  onSignOut: () => void;
  onSelectPatient: (patientId: number) => void;
  onStartTraining: () => void;
}

export class Dashboard extends Component {
  private config: DashboardConfig;

  constructor(config: DashboardConfig) {
    super();
    this.config = config;
  }

  render(): string {
    return `
      <div class="min-h-screen bg-slate-900 text-slate-100">
        <div class="bg-gradient-to-r from-cyan-900 to-blue-900 border-b border-cyan-500/30 p-4">
          <div class="max-w-7xl mx-auto flex justify-between items-center">
            <div class="flex items-center gap-4">
              <h1 class="text-2xl font-bold text-cyan-300">OpticTrigeminal</h1>
              <span class="text-xs bg-cyan-500/20 border border-cyan-500/50 px-3 py-1 rounded text-cyan-300">v3.0.0</span>
            </div>
            <div class="flex items-center gap-6">
              <div class="text-right">
                <div class="text-sm font-semibold">${this.config.currentStaffName}</div>
                <div class="text-xs text-slate-400">${this.config.currentRole?.toUpperCase()}</div>
              </div>
              <button
                id="btn-signout"
                class="px-4 py-2 bg-red-900/40 hover:bg-red-900/60 border border-red-700/50 rounded text-red-300 text-sm font-semibold"
              >
                Sign Out
              </button>
            </div>
          </div>
        </div>

        <div class="max-w-7xl mx-auto p-4">
          <div id="patient-grid" class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4 mb-8">
            ${this.renderPatientCards()}
          </div>
          
          <div id="training-section" class="bg-slate-800 border border-amber-700/50 rounded-lg p-6 mt-8">
            <h2 class="text-xl font-bold text-amber-300 mb-4">Clinical Training</h2>
            <button
              id="btn-start-training"
              class="px-6 py-3 bg-amber-700/40 hover:bg-amber-700/60 border border-amber-600/50 rounded text-amber-300 font-semibold"
            >
              Start Training Scenario
            </button>
          </div>
        </div>

        <div id="system-log" class="fixed bottom-4 right-4 max-w-sm max-h-64 bg-slate-950 border border-slate-700 rounded overflow-y-auto font-mono text-xs text-slate-400"></div>
      </div>
    `;
  }

  private renderPatientCards(): string {
    return this.config.patients.map(patient => `
      <div class="patient-card bg-slate-800 border border-slate-700 rounded-lg p-4 hover:border-cyan-500/50 transition cursor-pointer" data-patient-id="${patient.id}">
        <h3 class="font-bold text-lg text-cyan-300">${patient.name}</h3>
        <div class="text-xs text-slate-400 mb-4">${patient.room} | ${patient.mrn}</div>
        
        <div class="grid grid-cols-2 gap-2 text-sm">
          <div class="bg-slate-700 p-2 rounded">
            <div class="text-xs text-slate-500">HR</div>
            <div class="font-bold ${patient.vitals.hr > 100 ? 'text-red-400' : 'text-green-400'}">${patient.vitals.hr}</div>
          </div>
          <div class="bg-slate-700 p-2 rounded">
            <div class="text-xs text-slate-500">SpO2</div>
            <div class="font-bold ${patient.vitals.spo2 < 95 ? 'text-red-400' : 'text-green-400'}">${patient.vitals.spo2}%</div>
          </div>
          <div class="bg-slate-700 p-2 rounded">
            <div class="text-xs text-slate-500">RR</div>
            <div class="font-bold text-blue-400">${patient.vitals.rr}</div>
          </div>
          <div class="bg-slate-700 p-2 rounded">
            <div class="text-xs text-slate-500">BP</div>
            <div class="font-bold text-yellow-400">${patient.vitals.bp_sys}/${patient.vitals.bp_dia}</div>
          </div>
        </div>
      </div>
    `).join('');
  }

  mount(container: HTMLElement): void {
    container.innerHTML = this.render();
    this.element = container;
    this.setupEventListeners();
  }

  unmount(): void {
    this.element = null;
  }

  private setupEventListeners(): void {
    this.on('#btn-signout', 'click', () => {
      this.config.onSignOut();
    });

    this.on('.patient-card', 'click', (e) => {
      const card = (e.target as HTMLElement).closest('.patient-card') as HTMLElement;
      const patientId = parseInt(card.getAttribute('data-patient-id')!);
      this.config.onSelectPatient(patientId);
    });

    this.on('#btn-start-training', 'click', () => {
      this.config.onStartTraining();
    });
  }

  addLogEntry(message: string, type: 'info' | 'error' | 'success' = 'info'): void {
    const log = this.querySelector<HTMLElement>('#system-log');
    if (!log) return;

    const entry = document.createElement('div');
    const colors = {
      info: 'text-slate-400',
      error: 'text-red-400',
      success: 'text-green-400'
    };
    entry.className = `${colors[type]} border-t border-slate-700 pt-1 px-2 py-1`;
    entry.textContent = `[${type.toUpperCase()}] ${message}`;
    log.appendChild(entry);
    log.scrollTop = log.scrollHeight;
  }
}
