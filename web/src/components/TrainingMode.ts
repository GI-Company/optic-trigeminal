import { Component } from './component';
import type { TrainingSession } from '@api/types';

export interface TrainingModeConfig {
  session: TrainingSession;
  onAction: (action: string, params?: Record<string, any>) => void;
  onEnd: () => void;
}

export class TrainingMode extends Component {
  private config: TrainingModeConfig;

  constructor(config: TrainingModeConfig) {
    super();
    this.config = config;
  }

  render(): string {
    const session = this.config.session;

    return `
      <div class="min-h-screen bg-slate-900 text-slate-100">
        <div class="bg-gradient-to-r from-amber-900 to-orange-900 border-b border-amber-500/30 p-4">
          <div class="max-w-7xl mx-auto flex justify-between items-center">
            <div>
              <h1 class="text-2xl font-bold text-amber-300">${session.scenario.title}</h1>
              <div class="text-sm text-amber-200">Difficulty: ${session.scenario.difficulty}</div>
            </div>
            <div class="text-right">
              <div class="text-3xl font-bold text-amber-300">${(session.score * 100).toFixed(0)}%</div>
              <div class="text-sm text-amber-200">Time: ${Math.round(session.elapsed_ms / 1000)}s</div>
            </div>
          </div>
        </div>

        <div class="max-w-7xl mx-auto p-4">
          <div class="grid grid-cols-4 gap-4 mb-6">
            ${this.renderVitalCard('Heart Rate', session.patient.vitals.hr, 'bpm', 'text-red-400')}
            ${this.renderVitalCard('Respiratory Rate', session.patient.vitals.rr, 'breaths/min', 'text-orange-400')}
            ${this.renderVitalCard('SpO2', session.patient.vitals.spo2, '%', 'text-cyan-400')}
            ${this.renderVitalCard('Temperature', session.patient.vitals.temp.toFixed(1), '°C', 'text-yellow-400')}
          </div>

          <div class="grid grid-cols-2 gap-3 mb-6">
            <button 
              id="btn-assess" 
              class="training-action-btn py-2 px-4 bg-blue-900/40 hover:bg-blue-900/60 border border-blue-700/50 rounded text-blue-300 font-semibold"
            >
              Assess Patient
            </button>
            <button 
              id="btn-oxygen" 
              class="training-action-btn py-2 px-4 bg-green-900/40 hover:bg-green-900/60 border border-green-700/50 rounded text-green-300 font-semibold"
            >
              Administer Oxygen
            </button>
            <button 
              id="btn-position" 
              class="training-action-btn py-2 px-4 bg-purple-900/40 hover:bg-purple-900/60 border border-purple-700/50 rounded text-purple-300 font-semibold"
            >
              Position Patient
            </button>
            <button 
              id="btn-notify" 
              class="training-action-btn py-2 px-4 bg-red-900/40 hover:bg-red-900/60 border border-red-700/50 rounded text-red-300 font-semibold"
            >
              Notify Provider
            </button>
          </div>

          <button
            id="btn-end-training"
            class="w-full py-3 bg-slate-600 hover:bg-slate-500 rounded text-slate-100 font-bold"
          >
            End Training Session
          </button>
        </div>
      </div>
    `;
  }

  private renderVitalCard(label: string, value: number | string, unit: string, colorClass: string): string {
    return `
      <div class="bg-slate-800 border border-slate-700 p-4 rounded">
        <div class="text-xs text-slate-400">${label}</div>
        <div class="text-3xl font-bold ${colorClass}">${value}</div>
        <div class="text-xs text-slate-500">${unit}</div>
      </div>
    `;
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
    this.on('#btn-assess', 'click', () => {
      this.config.onAction('assess_patient');
    });

    this.on('#btn-oxygen', 'click', () => {
      this.config.onAction('administer_oxygen', { lpm: 2 });
    });

    this.on('#btn-position', 'click', () => {
      this.config.onAction('position_patient');
    });

    this.on('#btn-notify', 'click', () => {
      this.config.onAction('notify_provider');
    });

    this.on('#btn-end-training', 'click', () => {
      this.config.onEnd();
    });
  }
}
