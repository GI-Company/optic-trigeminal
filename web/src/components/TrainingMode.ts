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
      <div class="min-h-screen bg-slate-950 text-slate-100 relative overflow-hidden">
        <!-- Emergency Background Effects -->
        <div class="fixed top-[-20%] left-[-10%] w-[60%] h-[60%] rounded-full bg-amber-600/10 blur-[150px] pointer-events-none animate-pulse-slow"></div>
        
        <!-- Header -->
        <div class="glass-panel border-b border-amber-900/50 p-4 sticky top-0 z-20 bg-slate-950/80">
          <div class="max-w-7xl mx-auto flex justify-between items-center">
            <div class="flex items-center gap-4">
              <div class="w-10 h-10 rounded-lg bg-amber-500/20 border border-amber-500/50 flex items-center justify-center text-amber-400">
                <svg class="w-6 h-6" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 10V3L4 14h7v7l9-11h-7z"></path></svg>
              </div>
              <div>
                <h1 class="text-xl font-bold text-white tracking-tight">${session.scenario.title}</h1>
                <div class="flex items-center gap-2 mt-0.5">
                  <span class="px-2 py-0.5 rounded text-[10px] font-bold uppercase tracking-wider bg-amber-500/20 text-amber-400 border border-amber-500/30">
                    Difficulty: ${session.scenario.difficulty}
                  </span>
                </div>
              </div>
            </div>
            <div class="flex items-center gap-6">
              <div class="text-right">
                <div class="text-[10px] text-slate-400 uppercase tracking-wider font-bold mb-1">Session Score</div>
                <div id="tm-score" class="text-2xl font-bold text-amber-400 font-mono">${(session.score * 100).toFixed(0)}%</div>
              </div>
              <div class="h-10 w-px bg-slate-800"></div>
              <button id="btn-end-training" class="btn-outline-neon !text-red-400 !border-red-500/50 hover:!bg-red-500/10 px-4 py-2 text-sm flex items-center gap-2">
                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 12a9 9 0 11-18 0 9 9 0 0118 0z"></path><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 10a1 1 0 011-1h4a1 1 0 011 1v4a1 1 0 01-1 1h-4a1 1 0 01-1-1v-4z"></path></svg>
                End Session
              </button>
            </div>
          </div>
        </div>

        <div class="max-w-7xl mx-auto p-6 relative z-10 animate-fade-in-up">
          <!-- Vitals Monitor -->
          <div class="glass-card p-6 border-amber-900/30 mb-8">
            <h2 class="text-sm font-bold text-slate-400 uppercase tracking-wider mb-4 flex items-center gap-2">
              <div class="w-2 h-2 rounded-full bg-red-500 animate-pulse"></div>
              Live Patient Monitor
            </h2>
            <div class="grid grid-cols-2 md:grid-cols-4 gap-4">
              ${this.renderVitalCard('tm-vital-hr', 'Heart Rate', session.patient.vitals.hr, 'bpm', session.patient.vitals.hr > 100 ? 'text-red-500' : 'text-green-400')}
              ${this.renderVitalCard('tm-vital-spo2', 'SpO2', session.patient.vitals.spo2, '%', session.patient.vitals.spo2 < 95 ? 'text-red-500' : 'text-cyan-400')}
              ${this.renderVitalCard('tm-vital-rr', 'Resp Rate', session.patient.vitals.rr, 'rpm', session.patient.vitals.rr > 22 ? 'text-amber-500' : 'text-slate-200')}
              ${this.renderVitalCard('tm-vital-bp', 'Blood Pressure', `${session.patient.vitals.bp_sys}/${session.patient.vitals.bp_dia}`, 'mmHg', 'text-slate-300')}
            </div>
          </div>

          <div class="grid grid-cols-1 lg:grid-cols-2 gap-8">
            <!-- Scenario Information -->
            <div class="glass-card p-6">
              <h2 class="text-lg font-bold text-white mb-4">Scenario Briefing</h2>
              <p class="text-slate-300 text-sm leading-relaxed whitespace-pre-wrap">${session.scenario.description}</p>
              
              <div class="mt-8">
                <h3 class="text-sm font-bold text-slate-400 uppercase tracking-wider mb-4">Patient Profile</h3>
                <div class="bg-slate-900/50 rounded-lg p-4 border border-slate-800">
                  <div class="grid grid-cols-2 gap-4 text-sm">
                    <div>
                      <div class="text-slate-500 mb-1">Name</div>
                      <div class="text-white font-medium">${session.patient.name}</div>
                    </div>
                    <div>
                      <div class="text-slate-500 mb-1">Age / Gender</div>
                      <div class="text-white font-medium">${this.formatAgeGender(session.patient.name)}</div>
                    </div>
                    <div class="col-span-2">
                      <div class="text-slate-500 mb-1">Chief Complaint</div>
                      <div class="text-white font-medium">${session.patient.admission_diagnosis || 'Unknown'}</div>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            <!-- Interventions -->
            <div class="glass-card p-6 border-cyan-900/30">
              <h2 class="text-lg font-bold text-white mb-4 flex items-center gap-2">
                <svg class="w-5 h-5 text-cyan-500" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19.428 15.428a2 2 0 00-1.022-.547l-2.387-.477a6 6 0 00-3.86.517l-.318.158a6 6 0 01-3.86.517L6.05 15.21a2 2 0 00-1.806.547M8 4h8l-1 1v5.172a2 2 0 00.586 1.414l5 5c1.26 1.26.367 3.414-1.415 3.414H4.828c-1.782 0-2.674-2.154-1.414-3.414l5-5A2 2 0 009 10.172V5L8 4z"></path></svg>
                Available Interventions
              </h2>
              <div class="grid grid-cols-1 sm:grid-cols-2 gap-3">
                ${this.renderInterventionButtons()}
              </div>
              
              <!-- Simulation Log (Mock) -->
              <div class="mt-6 bg-slate-900/80 rounded-lg p-4 border border-slate-800 h-48 overflow-y-auto font-mono text-xs text-slate-300">
                <div class="mb-2 text-slate-500">>> Simulation Initialized</div>
                <div class="mb-2 text-slate-500">>> AI Kernel Attached</div>
                <div class="mb-2 text-cyan-400">>> Standing by for intervention...</div>
              </div>
            </div>
          </div>
        </div>
      </div>
    `;
  }

  // handle_training_start (src/server/http_server.cpp) bakes the synthetic
  // patient's real age/sex into their display name as "... (Age 62, M)"
  // since the shared Patient type has no age/sex fields of its own (it's
  // built around the real patient roster, not training scenarios). Parses
  // that back out instead of showing a hardcoded "65 / M" for every
  // scenario regardless of the actual synthetic patient.
  private formatAgeGender(name: string): string {
    const match = name.match(/Age (\d+), (\w+)\)/);
    return match ? `${match[1]} / ${match[2]}` : 'Unknown';
  }

  private renderVitalCard(id: string, label: string, value: number | string, unit: string, colorClass: string): string {
    return `
      <div class="bg-slate-900/50 border border-slate-800 p-4 rounded-xl flex flex-col items-center justify-center">
        <div class="text-[10px] text-slate-500 font-medium uppercase tracking-wider mb-1">${label}</div>
        <div id="${id}" class="text-3xl font-bold font-mono tracking-tighter ${colorClass}">${value}</div>
        <div class="text-[10px] text-slate-500 mt-1">${unit}</div>
      </div>
    `;
  }

  // Updates just the numbers that change on a tick/action, instead of the
  // full mount()/innerHTML replace every 4s (tick loop) or after every
  // click did before. A full innerHTML replacement destroys and recreates
  // every DOM node in the screen, including the outer wrapper carrying
  // .animate-fade-in-up -- so the whole screen re-ran its
  // fade-from-transparent entrance animation on every single update,
  // which is the "flash" on every refresh.
  updateLiveData(session: TrainingSession): void {
    this.config.session = session;
    if (!this.element) return;

    const setText = (id: string, text: string) => {
      const el = this.element!.querySelector(`#${id}`);
      if (el) el.textContent = text;
    };

    setText('tm-score', `${(session.score * 100).toFixed(0)}%`);
    setText('tm-vital-hr', String(session.patient.vitals.hr));
    setText('tm-vital-spo2', String(session.patient.vitals.spo2));
    setText('tm-vital-rr', String(session.patient.vitals.rr));
    setText('tm-vital-bp', `${session.patient.vitals.bp_sys}/${session.patient.vitals.bp_dia}`);
  }

  // Rendered from session.scenario.available_actions -- the exact action ids
  // the backend's ScenarioRuntime::evaluate_action_correctness actually
  // scores for this scenario. This used to be four fixed generic buttons
  // (assess_patient/administer_oxygen/position_patient/notify_provider)
  // that matched no scenario's evaluated actions at all, so every click fell
  // through to a catch-all that always scored "correct" -- the score went up
  // no matter what you pressed or when. See training_scenario.cpp for the
  // fix on the scoring side.
  private renderInterventionButtons(): string {
    const colorCycle = [
      'text-cyan-400 border-cyan-500/50 hover:bg-cyan-500/10',
      'text-blue-400 border-blue-500/50 hover:bg-blue-500/10',
      'text-emerald-400 border-emerald-500/50 hover:bg-emerald-500/10',
      'text-amber-400 border-amber-500/50 hover:bg-amber-500/10'
    ];
    return this.config.session.scenario.available_actions.map((action, i) => `
      <button class="intervention-btn training-action-btn btn-outline-neon !${colorCycle[i % colorCycle.length]} py-3 text-sm flex flex-col items-center justify-center gap-1 h-24" data-action-id="${action.id}" data-action-label="${action.label}">
        <svg class="w-6 h-6 mb-1" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 10V3L4 14h7v7l9-11h-7z"></path></svg>
        ${action.label}
      </button>
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
    const logAction = (actionLabel: string) => {
      const logContainer = this.element?.querySelector('.font-mono');
      if (logContainer) {
        const entry = document.createElement('div');
        entry.className = 'mb-2 text-cyan-300';
        entry.textContent = `>> Executed: ${actionLabel} (Awaiting Engine Response...)`;
        logContainer.appendChild(entry);
        logContainer.scrollTop = logContainer.scrollHeight;
      }
    };

    this.on('.intervention-btn', 'click', (e) => {
      const btn = e.currentTarget as HTMLButtonElement;
      const actionId = btn.dataset.actionId!;
      const actionLabel = btn.dataset.actionLabel || actionId;
      logAction(actionLabel);
      this.config.onAction(actionId);
    });

    this.on('#btn-end-training', 'click', () => {
      this.config.onEnd();
    });
  }
}
