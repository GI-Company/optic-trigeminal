import { Component } from './component';
import type { Patient, ChartEntry, RoleCapabilities } from '@api/types';

export type VitalKey = 'hr' | 'rr' | 'spo2' | 'temp';

export interface PatientDetailConfig {
  patient: Patient;
  chartEntries: ChartEntry[];
  currentStaffName: string;
  roleCapabilities: RoleCapabilities;
  onBack: () => void;
  onAddNote: (content: string) => void;
  onGenerateSBAR: () => void;
  onVitalClick: (vital: VitalKey) => void;
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
      <div class="min-h-screen bg-slate-950 text-slate-100 relative overflow-hidden">
        <!-- Background Effects -->
        <div class="fixed top-[-10%] left-[-10%] w-[40%] h-[40%] rounded-full bg-cyan-600/10 blur-[120px] pointer-events-none"></div>
        <div class="fixed bottom-[-10%] right-[-10%] w-[40%] h-[40%] rounded-full bg-blue-600/10 blur-[120px] pointer-events-none"></div>

        <div class="glass-panel border-b border-slate-800 p-4 sticky top-0 z-20">
          <div class="max-w-6xl mx-auto flex items-center">
            <button id="btn-back" class="btn-outline-neon px-4 py-2 text-sm flex items-center gap-2">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 19l-7-7m0 0l7-7m-7 7h18"></path></svg>
              Dashboard
            </button>
          </div>
        </div>

        <div class="max-w-6xl mx-auto p-6 relative z-10 animate-fade-in-up">
          <div class="flex items-center gap-4 mb-8">
            <div class="w-16 h-16 rounded-2xl bg-gradient-to-br from-cyan-500 to-blue-600 flex items-center justify-center text-2xl font-bold text-white shadow-lg shadow-cyan-500/20">
              ${patient.name.charAt(0)}
            </div>
            <div>
              <h1 class="text-3xl font-bold text-white tracking-tight">${patient.name}</h1>
              <div class="flex items-center gap-3 mt-1 text-sm text-slate-400">
                <span class="inline-flex items-center bg-slate-800 rounded-full px-3 py-0.5 border border-slate-700">MRN: ${patient.mrn}</span>
                <span class="inline-flex items-center bg-slate-800 rounded-full px-3 py-0.5 border border-slate-700">Room ${patient.room}</span>
              </div>
            </div>
          </div>

          <div class="grid grid-cols-2 md:grid-cols-4 gap-4 mb-8">
            ${this.renderVitalsCard('hr', 'Heart Rate', patient.vitals.hr, 'bpm', patient.vitals.hr > 100 ? 'text-red-400' : 'text-cyan-400')}
            ${this.renderVitalsCard('rr', 'Respiratory Rate', patient.vitals.rr, 'breaths/min', 'text-slate-200')}
            ${this.renderVitalsCard('spo2', 'Oxygen Sat.', patient.vitals.spo2, '%', patient.vitals.spo2 < 95 ? 'text-red-400' : 'text-cyan-400')}
            ${this.renderVitalsCard('temp', 'Temperature', patient.vitals.temp.toFixed(1), '°C', 'text-amber-400')}
          </div>

          <div class="grid grid-cols-1 lg:grid-cols-3 gap-8">
            <div class="lg:col-span-2 space-y-6">
              <div id="chart-area" class="glass-card p-6 h-[500px] flex flex-col">
                <h2 class="text-xl font-bold text-white mb-4 flex items-center gap-2">
                  <svg class="w-5 h-5 text-cyan-500" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12h6m-6 4h6m2 5H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z"></path></svg>
                  Clinical Chart
                </h2>
                <div id="chart-entries" class="flex-1 overflow-y-auto space-y-3 pr-2">
                  ${this.renderChartEntries()}
                </div>
              </div>
            </div>

            <div class="space-y-6">
              ${caps.canChartActions ? `
                <div class="glass-card p-6 border-cyan-900/50">
                  <h2 class="text-lg font-bold text-white mb-4">Documentation</h2>
                  <textarea
                    id="note-input"
                    class="w-full bg-slate-900/50 border border-slate-700 rounded-lg p-3 text-sm text-slate-200 placeholder-slate-500 focus:border-cyan-500 focus:ring-1 focus:ring-cyan-500 focus:outline-none transition-all mb-4 resize-none"
                    rows="4"
                    placeholder="Enter clinical observations..."
                  ></textarea>
                  <div class="flex flex-col gap-3">
                    <button id="btn-add-note" class="btn-neon w-full py-2.5 text-sm">
                      Sign & Submit Note
                    </button>
                    <button id="btn-suggest-sbar" class="btn-outline-neon w-full py-2.5 text-sm flex items-center justify-center gap-2">
                      <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 10V3L4 14h7v7l9-11h-7z"></path></svg>
                      Auto-Generate SBAR
                    </button>
                  </div>
                </div>
              ` : ''}
            </div>
          </div>
        </div>
      </div>
    `;
  }

  private renderVitalsCard(vital: VitalKey, label: string, value: string | number, unit: string, colorClass: string): string {
    return `
      <div class="vital-card glass-card p-5 relative overflow-hidden group cursor-pointer hover:border-cyan-500/40 transition-colors" data-vital="${vital}" role="button" tabindex="0" aria-label="View ${label} history">
        <div class="absolute inset-0 bg-gradient-to-br from-white/5 to-transparent opacity-0 group-hover:opacity-100 transition-opacity"></div>
        <div class="text-[11px] text-slate-500 font-medium uppercase tracking-wider mb-2">${label}</div>
        <div class="flex items-baseline gap-1">
          <div id="vital-value-${vital}" class="text-4xl font-bold ${colorClass}">${value}</div>
          <div class="text-xs text-slate-500 font-medium">${unit}</div>
        </div>
        <div class="mt-2 text-[10px] text-slate-600 group-hover:text-cyan-400 transition-colors">View history →</div>
      </div>
    `;
  }

  private renderChartEntries(): string {
    if (this.config.chartEntries.length === 0) {
      return `
        <div class="flex flex-col items-center justify-center h-full text-slate-500 space-y-4">
          <svg class="w-12 h-12 opacity-20" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20 13V6a2 2 0 00-2-2H6a2 2 0 00-2 2v7m16 0v5a2 2 0 01-2 2H6a2 2 0 01-2-2v-5m16 0h-2.586a1 1 0 00-.707.293l-2.414 2.414a1 1 0 01-.707.293h-3.172a1 1 0 01-.707-.293l-2.414-2.414A1 1 0 006.586 13H4"></path></svg>
          <p>No chart entries recorded</p>
        </div>
      `;
    }

    return this.config.chartEntries.map(entry => `
      <div class="bg-slate-900/40 border border-slate-700/50 rounded-lg p-4 hover:border-cyan-500/30 transition-colors">
        <div class="flex justify-between items-center mb-3">
          <span class="inline-flex items-center px-2 py-0.5 rounded text-[10px] font-bold bg-cyan-500/10 text-cyan-400 border border-cyan-500/20 uppercase tracking-wider">${entry.type}</span>
          <span class="text-xs text-slate-500 font-mono">${new Date(entry.timestamp).toLocaleTimeString()}</span>
        </div>
        <p class="text-slate-300 text-sm whitespace-pre-wrap leading-relaxed">${entry.content}</p>
        <div class="mt-3 pt-3 border-t border-slate-800 flex justify-between items-center">
          <span class="text-xs text-slate-500 flex items-center gap-1">
            <svg class="w-3 h-3" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M16 7a4 4 0 11-8 0 4 4 0 018 0zM12 14a7 7 0 00-7 7h14a7 7 0 00-7-7z"></path></svg>
            ${entry.nurse}
          </span>
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
    this.on('#btn-back', 'click', () => {
      this.config.onBack();
    });

    this.on('.vital-card', 'click', (e) => {
      const card = (e.currentTarget as HTMLElement);
      const vital = card.dataset.vital as VitalKey | undefined;
      if (vital) this.config.onVitalClick(vital);
    });

    if (this.config.roleCapabilities.canChartActions) {
      this.on('#btn-add-note', 'click', () => {
        const input = this.querySelector<HTMLTextAreaElement>('#note-input');
        if (input && input.value.trim()) {
          // onAddNote now persists to the server (see main-refactored.ts)
          // before it's actually safe to show the note as charted -- it
          // calls refreshChart() itself once that completes, with the
          // up-to-date entry list. Refreshing here too, immediately and
          // with no new data, would just redraw the still-stale list a
          // beat before the real update arrives.
          this.config.onAddNote(input.value);
          input.value = '';
        }
      });

      this.on('#btn-suggest-sbar', 'click', () => {
        this.config.onGenerateSBAR();
      });
    }
  }

  // `this.config.chartEntries` is a snapshot taken when this component was
  // constructed (store.getChartEntries() returns a fresh copy, not a live
  // reference -- see web/src/store/state.ts) -- re-rendering from it alone
  // would keep showing that same snapshot forever no matter how many notes
  // get added afterward. Callers now pass the current entries in.
  refreshChart(entries: ChartEntry[]): void {
    this.config.chartEntries = entries;
    const chartArea = this.querySelector<HTMLElement>('#chart-entries');
    if (chartArea) {
      chartArea.innerHTML = this.renderChartEntries();
      chartArea.scrollTop = chartArea.scrollHeight;
    }
  }

  // Patches just the four vital numbers (+ their threshold-driven color) in
  // place, the same targeted-update pattern as TrainingMode.updateLiveData --
  // this used to have no live-update path at all: setupAutoRefresh()
  // (main-refactored.ts) only polled the server while on the dashboard
  // route, so a patient's vitals silently froze at whatever they were the
  // moment you opened their detail page (and any counterfactual-fork /
  // attribution-band UI opened from here inherited that same stale
  // snapshot) until you navigated back out and in again. Deliberately
  // patches text/class on existing nodes rather than replacing the
  // .vital-card outerHTML -- that click handler is bound with a direct
  // addEventListener (see Component.on), not delegation, so swapping the
  // node would silently drop "View history" for that vital.
  updateVitals(patient: Patient): void {
    this.config.patient = patient;
    if (!this.element) return;

    const setVital = (vital: VitalKey, value: string, colorClass?: string) => {
      const el = this.element!.querySelector<HTMLElement>(`#vital-value-${vital}`);
      if (!el) return;
      el.textContent = value;
      if (colorClass) el.className = `text-4xl font-bold ${colorClass}`;
    };

    setVital('hr', String(patient.vitals.hr), patient.vitals.hr > 100 ? 'text-red-400' : 'text-cyan-400');
    setVital('rr', String(patient.vitals.rr));
    setVital('spo2', String(patient.vitals.spo2), patient.vitals.spo2 < 95 ? 'text-red-400' : 'text-cyan-400');
    setVital('temp', patient.vitals.temp.toFixed(1));
  }
}
