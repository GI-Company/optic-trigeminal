import { Component } from './component';
import type { Patient, ChartEntry, PatientObservation } from '@api/types';
import type { VitalKey } from './PatientDetail';

export interface VitalHistoryPanelConfig {
  vital: VitalKey;
  patient: Patient;
  observations: PatientObservation[];
  chartEntries: ChartEntry[];
  onClose: () => void;
}

interface VitalMeta {
  label: string;
  unit: string;
  color: string; // stroke color when this vital is the one selected
  history: (p: Patient) => number[];
  current: (p: Patient) => number;
  // Substrings (case-insensitive) used to match this vital's own
  // observations out of the full per-patient observation list -- a
  // heuristic, not a new backend field, since PatientObservation isn't
  // tagged with which vital it's about.
  keywords: string[];
}

const VITALS: Record<VitalKey, VitalMeta> = {
  hr: {
    label: 'Heart Rate', unit: 'bpm', color: '#22d3ee',
    history: p => p.hr_history, current: p => p.vitals.hr,
    keywords: ['heart rate', 'hr ']
  },
  rr: {
    label: 'Respiratory Rate', unit: 'breaths/min', color: '#a78bfa',
    history: p => p.rr_history, current: p => p.vitals.rr,
    keywords: ['respiratory rate', 'respiratory compromise']
  },
  spo2: {
    label: 'Oxygen Sat.', unit: '%', color: '#60a5fa',
    history: p => p.spo2_history, current: p => p.vitals.spo2,
    keywords: ['spo2', 'oxygen', 'hypoxia', 'hypoxemia']
  },
  temp: {
    label: 'Temperature', unit: '°C', color: '#fbbf24',
    history: p => p.temp_history, current: p => p.vitals.temp,
    keywords: ['temperature']
  }
};

const CHART_WIDTH = 600;
const CHART_HEIGHT = 180;
const PAD = 28;

// Overlay panel (same backdrop+glass-card visual language as Modal.ts,
// mounted into #modal-root the same way TemporalControls.ts does) showing
// one vital's real sampled history, the other three overlaid for visual
// cross-correlation, any chart notes that fall inside the visible time
// window, and real auto-generated findings for this vital pulled from
// ClinicalAnalyzer (via /api/clinical/observations) -- nothing here is
// invented: an empty section means there's genuinely nothing to show yet.
export class VitalHistoryPanel extends Component {
  private config: VitalHistoryPanelConfig;

  constructor(config: VitalHistoryPanelConfig) {
    super();
    this.config = config;
  }

  render(): string {
    const { vital, patient } = this.config;
    const meta = VITALS[vital];
    const current = meta.current(patient);
    const displayValue = vital === 'temp' ? (current as number).toFixed(1) : String(current);

    return `
      <div id="vital-panel-backdrop" class="fixed inset-0 z-[100] bg-black/60 backdrop-blur-sm flex items-center justify-center p-4">
        <div class="glass-card w-full max-w-2xl p-6 relative max-h-[90vh] overflow-y-auto" role="dialog" aria-modal="true">
          <div class="flex items-center justify-between mb-1">
            <h3 class="text-lg font-semibold text-white">${meta.label} History</h3>
            <button id="vital-panel-close" class="text-slate-400 hover:text-white text-xl leading-none">&times;</button>
          </div>
          <p class="text-xs text-slate-500 mb-4">Current: <span class="font-semibold" style="color:${meta.color}">${displayValue} ${meta.unit}</span> · last ${meta.history(patient).length} samples for this visit</p>

          ${this.renderChart()}
          ${this.renderLegend()}
          ${this.renderCorrelatedEntries()}
          ${this.renderExplanation()}
        </div>
      </div>
    `;
  }

  private renderChart(): string {
    const { patient, chartEntries } = this.config;
    const timestamps = patient.history_timestamps;
    if (!timestamps || timestamps.length === 0) {
      return `<div class="bg-slate-900/50 border border-slate-800 rounded-lg p-4 mb-4 text-sm text-slate-500">No history samples recorded yet.</div>`;
    }

    const minTs = Math.min(...timestamps);
    const maxTs = Math.max(...timestamps);
    const xOf = (ts: number) => {
      const frac = maxTs > minTs ? (ts - minTs) / (maxTs - minTs) : 0.5;
      return PAD + frac * (CHART_WIDTH - 2 * PAD);
    };

    const lines = (Object.keys(VITALS) as VitalKey[]).map(key => {
      const meta = VITALS[key];
      const values = meta.history(patient);
      if (!values || values.length === 0) return '';
      const min = Math.min(...values);
      const max = Math.max(...values);
      const yOf = (v: number) => {
        const frac = max > min ? (v - min) / (max - min) : 0.5;
        return CHART_HEIGHT - PAD - frac * (CHART_HEIGHT - 2 * PAD);
      };
      const points = values.map((v, i) => `${xOf(timestamps[i] ?? timestamps[timestamps.length - 1]).toFixed(1)},${yOf(v).toFixed(1)}`).join(' ');
      const isSelected = key === this.config.vital;
      return `<polyline points="${points}" fill="none" stroke="${meta.color}" stroke-width="${isSelected ? 2.5 : 1.2}" opacity="${isSelected ? 1 : 0.35}" stroke-linejoin="round" stroke-linecap="round" />`;
    }).join('');

    // Chart notes/observations whose real timestamp falls inside this
    // vital's visible sample window -- the actual, timestamp-based
    // correlation between vitals and charting the previous data model
    // couldn't do (vitals history had no timestamps at all before now).
    const markers = chartEntries
      .filter(e => {
        const ts = new Date(e.timestamp).getTime() / 1000;
        return ts >= minTs && ts <= maxTs;
      })
      .map(e => {
        const ts = new Date(e.timestamp).getTime() / 1000;
        const x = xOf(ts);
        return `<circle cx="${x.toFixed(1)}" cy="${CHART_HEIGHT - PAD + 8}" r="3.5" fill="#f472b6"><title>${this.escape(e.type)} @ ${new Date(e.timestamp).toLocaleTimeString()}: ${this.escape(e.content.slice(0, 80))}</title></circle>`;
      }).join('');

    return `
      <div class="bg-slate-900/50 border border-slate-800 rounded-lg p-3 mb-3">
        <svg viewBox="0 0 ${CHART_WIDTH} ${CHART_HEIGHT}" class="w-full h-auto" preserveAspectRatio="none">
          <line x1="${PAD}" y1="${CHART_HEIGHT - PAD}" x2="${CHART_WIDTH - PAD}" y2="${CHART_HEIGHT - PAD}" stroke="#334155" stroke-width="1" />
          ${lines}
          ${markers}
        </svg>
        <div class="flex justify-between text-[10px] text-slate-600 mt-1">
          <span>${new Date(minTs * 1000).toLocaleTimeString()}</span>
          <span>${new Date(maxTs * 1000).toLocaleTimeString()}</span>
        </div>
      </div>
    `;
  }

  private renderLegend(): string {
    const selected = this.config.vital;
    return `
      <div class="flex flex-wrap gap-3 mb-4 text-[11px]">
        ${(Object.keys(VITALS) as VitalKey[]).map(key => {
          const meta = VITALS[key];
          const isSelected = key === selected;
          return `<span class="inline-flex items-center gap-1.5 ${isSelected ? 'text-white font-semibold' : 'text-slate-500'}">
            <span class="w-2.5 h-2.5 rounded-full inline-block" style="background:${meta.color}; opacity:${isSelected ? 1 : 0.5}"></span>${meta.label}
          </span>`;
        }).join('')}
        <span class="inline-flex items-center gap-1.5 text-slate-500 ml-auto"><span class="w-2 h-2 rounded-full inline-block bg-pink-400"></span>Chart entry</span>
      </div>
      <p class="text-[10px] text-slate-600 -mt-2 mb-4">Other vitals are shown normalized to their own range for visual pattern-matching, not on ${VITALS[selected].label}'s absolute scale.</p>
    `;
  }

  private renderCorrelatedEntries(): string {
    const { patient, chartEntries } = this.config;
    const timestamps = patient.history_timestamps || [];
    if (timestamps.length === 0) return '';
    const minTs = Math.min(...timestamps);
    const maxTs = Math.max(...timestamps);
    const inWindow = chartEntries.filter(e => {
      const ts = new Date(e.timestamp).getTime() / 1000;
      return ts >= minTs && ts <= maxTs;
    });

    if (inWindow.length === 0) {
      return `<div class="mb-4"><h4 class="text-sm font-semibold text-white mb-2">Chart Entries in This Window</h4><p class="text-xs text-slate-500">No chart notes or observations fall within the visible history window.</p></div>`;
    }

    return `
      <div class="mb-4">
        <h4 class="text-sm font-semibold text-white mb-2">Chart Entries in This Window (${inWindow.length})</h4>
        <ul class="space-y-1.5">
          ${inWindow.map(e => `
            <li class="text-xs text-slate-300 bg-slate-900/40 border border-slate-800 rounded px-2 py-1.5">
              <span class="text-pink-400 font-mono">${new Date(e.timestamp).toLocaleTimeString()}</span>
              <span class="text-slate-500 mx-1">·</span>
              <span class="uppercase text-[10px] text-slate-500">${this.escape(e.type)}</span>
              <div class="text-slate-300 mt-0.5">${this.escape(e.content.length > 140 ? e.content.slice(0, 140) + '...' : e.content)}</div>
            </li>
          `).join('')}
        </ul>
      </div>
    `;
  }

  private renderExplanation(): string {
    const meta = VITALS[this.config.vital];
    const matches = this.config.observations.filter(o =>
      meta.keywords.some(kw => o.description.toLowerCase().includes(kw))
    );

    if (matches.length === 0) {
      return `
        <div class="bg-slate-900/50 border border-slate-800 rounded-lg p-4">
          <h4 class="text-sm font-semibold text-white mb-1">Auto-Suggested Explanation</h4>
          <p class="text-xs text-slate-500">No current finding for ${meta.label} -- ACmK-OT hasn't flagged a significant trend or threshold crossing for this vital right now.</p>
        </div>
      `;
    }

    return `
      <div class="bg-slate-900/50 border border-slate-800 rounded-lg p-4">
        <h4 class="text-sm font-semibold text-white mb-2">Auto-Suggested Explanation</h4>
        <ul class="space-y-2">
          ${matches.map(o => `
            <li class="text-xs bg-slate-950/50 border border-slate-800 rounded px-3 py-2">
              <div class="flex items-center gap-2 mb-1">
                <span class="uppercase text-[10px] px-1.5 py-0.5 rounded font-bold ${o.severity === 'critical' ? 'bg-red-500/10 text-red-400 border border-red-500/20' : o.severity === 'warning' ? 'bg-amber-500/10 text-amber-400 border border-amber-500/20' : 'bg-slate-800 text-slate-400'}">${o.severity}</span>
                <span class="text-slate-300 font-medium">${this.escape(o.description)}</span>
              </div>
              <p class="text-slate-400">${this.escape(o.rationale)}</p>
            </li>
          `).join('')}
        </ul>
      </div>
    `;
  }

  private escape(s: string): string {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  mount(container: HTMLElement): void {
    container.innerHTML = this.render();
    this.element = container;
    this.setupEventListeners();
  }

  unmount(): void {
    if (this.element) this.element.innerHTML = '';
    this.element = null;
  }

  private setupEventListeners(): void {
    this.on('#vital-panel-close', 'click', () => this.config.onClose());
    this.on('#vital-panel-backdrop', 'click', (e) => {
      if (e.target === e.currentTarget) this.config.onClose();
    });
  }
}
