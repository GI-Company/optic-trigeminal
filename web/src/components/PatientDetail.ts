import { Component } from './component';
import type { Patient, ChartEntry, RoleCapabilities } from '@api/types';

export interface PatientDetailConfig {
  patient: Patient;
  chartEntries: ChartEntry[];
  currentStaffName: string;
  roleCapabilities: RoleCapabilities;
  onBack: () => void;
  onAddNote: (content: string) => void;
  onGenerateSBAR: () => void;
}

export class PatientDetail extends Component {
  private config: PatientDetailConfig;

  constructor(config: PatientDetailConfig) {
    super();
    this.config = config;
  }

  render(): string {
    const patient = this.config.patient;
    const caps = this.config.roleCapabilities;

    return `
      <div class="min-h-screen bg-slate-900 text-slate-100">
        <div class="bg-slate-800 border-b border-slate-700 p-4 flex gap-4">
          <button 
            id="btn-back" 
            class="px-4 py-2 bg-slate-700 hover:bg-slate-600 rounded font-semibold"
          >
            ← Back to Dashboard
          </button>
        </div>

        <div class="max-w-6xl mx-auto p-4">
          <h1 class="text-3xl font-bold text-cyan-300 mb-6">${patient.name}</h1>

          <div class="grid grid-cols-4 gap-4 mb-6">
            ${this.renderVitalsCard('Heart Rate', patient.vitals.hr, 'bpm', 'text-red-400')}
            ${this.renderVitalsCard('Respiratory Rate', patient.vitals.rr, 'breaths/min', 'text-orange-400')}
            ${this.renderVitalsCard('Oxygen Saturation', patient.vitals.spo2, '%', 'text-cyan-400')}
            ${this.renderVitalsCard('Temperature', patient.vitals.temp.toFixed(1), '°C', 'text-yellow-400')}
          </div>

          ${caps.canChartActions ? `
            <div class="bg-slate-800 border border-cyan-500/30 rounded-lg p-6 mb-6">
              <h2 class="text-xl font-bold text-cyan-300 mb-4">Clinical Documentation</h2>
              <textarea
                id="note-input"
                class="w-full bg-slate-700 border border-slate-600 rounded p-3 text-slate-100 placeholder-slate-500 focus:border-cyan-500 focus:outline-none mb-3"
                rows="3"
                placeholder="Add clinical notes..."
              ></textarea>
              <div class="flex gap-3">
                <button 
                  id="btn-add-note" 
                  class="flex-1 py-2 bg-cyan-600 hover:bg-cyan-500 rounded text-white font-semibold"
                >
                  Add Note
                </button>
                <button 
                  id="btn-suggest-sbar" 
                  class="flex-1 py-2 bg-blue-600 hover:bg-blue-500 rounded text-white font-semibold"
                >
                  Generate SBAR
                </button>
              </div>
            </div>
          ` : ''}

          <div id="chart-area" class="bg-slate-800 border border-slate-700 rounded-lg p-6">
            <h2 class="text-xl font-bold text-cyan-300 mb-4">Clinical Chart</h2>
            <div id="chart-entries" class="max-h-96 overflow-y-auto space-y-2">
              ${this.renderChartEntries()}
            </div>
          </div>
        </div>
      </div>
    `;
  }

  private renderVitalsCard(label: string, value: string | number, unit: string, colorClass: string): string {
    return `
      <div class="bg-slate-800 border border-slate-700 p-4 rounded">
        <div class="text-sm text-slate-400">${label}</div>
        <div class="text-4xl font-bold ${colorClass}">${value}</div>
        <div class="text-xs text-slate-500">${unit}</div>
      </div>
    `;
  }

  private renderChartEntries(): string {
    if (this.config.chartEntries.length === 0) {
      return '<p class="text-slate-500">No chart entries yet</p>';
    }

    return this.config.chartEntries.map(entry => `
      <div class="bg-slate-700 border border-slate-600 rounded p-3">
        <div class="flex justify-between items-start mb-2">
          <span class="text-xs font-semibold text-cyan-400 uppercase">${entry.type}</span>
          <span class="text-xs text-slate-500">${new Date(entry.timestamp).toLocaleTimeString()}</span>
        </div>
        <p class="text-slate-300 text-sm whitespace-pre-wrap">${entry.content}</p>
        <div class="text-xs text-slate-500 mt-2">by ${entry.nurse}</div>
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
    this.on('#btn-back', 'click', () => {
      this.config.onBack();
    });

    if (this.config.roleCapabilities.canChartActions) {
      this.on('#btn-add-note', 'click', () => {
        const input = this.querySelector<HTMLTextAreaElement>('#note-input');
        if (input && input.value.trim()) {
          this.config.onAddNote(input.value);
          input.value = '';
          this.refreshChart();
        }
      });

      this.on('#btn-suggest-sbar', 'click', () => {
        this.config.onGenerateSBAR();
      });
    }
  }

  refreshChart(): void {
    const chartArea = this.querySelector<HTMLElement>('#chart-entries');
    if (chartArea) {
      chartArea.innerHTML = this.renderChartEntries();
    }
  }
}
