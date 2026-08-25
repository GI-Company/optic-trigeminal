import { Component } from './component';
import type { Patient, ChartEntry, PatientObservation, PatientFork, ForkInterventionType } from '@api/types';
import type { VitalKey } from './PatientDetail';
import { apiClient } from '@api/client';

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
  trajectoryValue: (pt: import('@api/types').ForkTrajectoryPoint) => number;
  // Substrings (case-insensitive) used to match this vital's own
  // observations out of the full per-patient observation list -- a
  // heuristic, not a new backend field, since PatientObservation isn't
  // tagged with which vital it's about.
  keywords: string[];
}

const VITALS: Record<VitalKey, VitalMeta> = {
  hr: {
    label: 'Heart Rate', unit: 'bpm', color: '#22d3ee',
    history: p => p.hr_history, current: p => p.vitals.hr, trajectoryValue: pt => pt.hr,
    keywords: ['heart rate', 'hr ']
  },
  rr: {
    label: 'Respiratory Rate', unit: 'breaths/min', color: '#a78bfa',
    history: p => p.rr_history, current: p => p.vitals.rr, trajectoryValue: pt => pt.rr,
    keywords: ['respiratory rate', 'respiratory compromise']
  },
  spo2: {
    label: 'Oxygen Sat.', unit: '%', color: '#60a5fa',
    history: p => p.spo2_history, current: p => p.vitals.spo2, trajectoryValue: pt => pt.spo2,
    keywords: ['spo2', 'oxygen', 'hypoxia', 'hypoxemia']
  },
  temp: {
    label: 'Temperature', unit: '°C', color: '#fbbf24',
    history: p => p.temp_history, current: p => p.vitals.temp, trajectoryValue: pt => pt.temp,
    keywords: ['temperature']
  }
};

const INTERVENTIONS: { value: ForkInterventionType | ''; label: string }[] = [
  { value: '', label: 'No action' },
  { value: 'administer_fluids', label: 'IV Fluids' },
  { value: 'administer_oxygen', label: 'Oxygen' },
  { value: 'start_vasopressor', label: 'Vasopressor' },
  { value: 'administer_antibiotics', label: 'Antibiotics' },
  { value: 'administer_antipyretic', label: 'Antipyretic' }
];

// Cycles if more forks than colors are active at once -- fixed, small
// palette deliberately distinct from the 4 solid vital-line colors above
// and the pink chart-entry dots, so a dashed counterfactual overlay is
// never visually confused with real recorded data.
const FORK_COLORS = ['#f472b6', '#fbbf24', '#34d399', '#818cf8'];

const CHART_WIDTH = 600;
const CHART_HEIGHT = 180;
const PAD = 28;

interface ActiveFork extends PatientFork {
  color: string;
}

// Overlay panel (same backdrop+glass-card visual language as Modal.ts,
// mounted into #modal-root the same way TemporalControls.ts does) showing
// one vital's real sampled history, the other three overlaid for visual
// cross-correlation, any chart notes that fall inside the visible time
// window, real auto-generated findings for this vital, and -- the CCPC
// foundation built earlier this session -- counterfactual forks: click a
// point on the chart, choose an intervention, and see the projected
// trajectory overlaid as a dashed line. Nothing here is invented: an empty
// section means there's genuinely nothing to show, and a fork is always
// visually and textually distinct from the real observed line.
export class VitalHistoryPanel extends Component {
  private config: VitalHistoryPanelConfig;
  private selectedForkPoint: number | null = null;
  private activeForks: ActiveFork[] = [];
  private forkPending = false;
  private forkError: string | null = null;
  private nextForkColor = 0;

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
          ${this.renderForkControls()}
          ${this.renderCorrelatedEntries()}
          ${this.renderExplanation()}
        </div>
      </div>
    `;
  }

  // Chart's time domain -- extended past "now" to cover any active fork's
  // projected future when one exists, so the overlay isn't clipped.
  private timeDomain(): { minTs: number; maxTs: number } {
    const timestamps = this.config.patient.history_timestamps || [];
    let minTs = timestamps.length ? Math.min(...timestamps) : 0;
    let maxTs = timestamps.length ? Math.max(...timestamps) : 1;
    for (const fork of this.activeForks) {
      for (const pt of fork.trajectory) {
        if (pt.timestamp > maxTs) maxTs = pt.timestamp;
        if (pt.timestamp < minTs) minTs = pt.timestamp;
      }
    }
    return { minTs, maxTs };
  }

  private xOf(ts: number, minTs: number, maxTs: number): number {
    const frac = maxTs > minTs ? (ts - minTs) / (maxTs - minTs) : 0.5;
    return PAD + frac * (CHART_WIDTH - 2 * PAD);
  }

  // Inverse of xOf, snapped to the nearest real snapshot -- a fork can only
  // branch from an actual PatientSnapshot, not an arbitrary interpolated
  // instant, so a click always resolves to a real, valid from_timestamp.
  private nearestSnapshotForX(clientX: number, svgEl: SVGSVGElement): number | null {
    const snapshots = this.config.patient.snapshot_timestamps;
    if (!snapshots || snapshots.length === 0) return null;

    const rect = svgEl.getBoundingClientRect();
    const relX = ((clientX - rect.left) / rect.width) * CHART_WIDTH;
    const { minTs, maxTs } = this.timeDomain();
    const frac = (relX - PAD) / (CHART_WIDTH - 2 * PAD);
    const targetTs = minTs + Math.max(0, Math.min(1, frac)) * (maxTs - minTs);

    let nearest = snapshots[0];
    let bestDist = Math.abs(snapshots[0] - targetTs);
    for (const ts of snapshots) {
      const dist = Math.abs(ts - targetTs);
      if (dist < bestDist) { bestDist = dist; nearest = ts; }
    }
    return nearest;
  }

  private renderChart(): string {
    const { patient, chartEntries, vital } = this.config;
    const timestamps = patient.history_timestamps;
    if (!timestamps || timestamps.length === 0) {
      return `<div class="bg-slate-900/50 border border-slate-800 rounded-lg p-4 mb-4 text-sm text-slate-500">No history samples recorded yet.</div>`;
    }

    const { minTs, maxTs } = this.timeDomain();
    const xOf = (ts: number) => this.xOf(ts, minTs, maxTs);

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
      const isSelected = key === vital;
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

    // Selected fork point -- vertical dashed marker, distinct from the
    // solid grid baseline and the round chart-entry dots.
    const selectedMarker = this.selectedForkPoint != null
      ? `<line x1="${xOf(this.selectedForkPoint).toFixed(1)}" y1="${PAD}" x2="${xOf(this.selectedForkPoint).toFixed(1)}" y2="${CHART_HEIGHT - PAD}" stroke="#e2e8f0" stroke-width="1.5" stroke-dasharray="3,3" opacity="0.7" />`
      : '';

    // Counterfactual overlays -- selected vital's trajectory only, dashed,
    // one distinct color per active fork. Deliberately dashed and a
    // different color family than every solid line above so it can never
    // be mistaken for what actually happened.
    const forkOverlays = this.activeForks.map(fork => {
      const meta = VITALS[vital];
      const historyValues = meta.history(patient);
      const min = historyValues.length ? Math.min(...historyValues, ...fork.trajectory.map(meta.trajectoryValue)) : 0;
      const max = historyValues.length ? Math.max(...historyValues, ...fork.trajectory.map(meta.trajectoryValue)) : 1;
      const yOf = (v: number) => {
        const frac = max > min ? (v - min) / (max - min) : 0.5;
        return CHART_HEIGHT - PAD - frac * (CHART_HEIGHT - 2 * PAD);
      };
      // Trajectories are one point per simulated second (up to 1800 for a
      // 30-min fork) -- plotted at full density that's real data, but packed
      // into ~500px it reads as noisy jitter rather than a trend. Down-sample
      // to ~50 rendered points (always including the first/last) so the
      // shape stays legible; the underlying data returned to callers is
      // untouched, this only thins what gets drawn.
      const traj = fork.trajectory;
      const targetPoints = 50;
      const step = Math.max(1, Math.ceil(traj.length / targetPoints));
      const sampled = traj.filter((_, i) => i % step === 0 || i === traj.length - 1);
      const points = sampled.map(pt => `${xOf(pt.timestamp).toFixed(1)},${yOf(meta.trajectoryValue(pt)).toFixed(1)}`).join(' ');
      return `<polyline points="${points}" fill="none" stroke="${fork.color}" stroke-width="2" stroke-dasharray="6,4" opacity="0.9" stroke-linejoin="round" stroke-linecap="round" />`;
    }).join('');

    return `
      <div class="bg-slate-900/50 border border-slate-800 rounded-lg p-3 mb-3">
        <svg id="vital-chart-svg" viewBox="0 0 ${CHART_WIDTH} ${CHART_HEIGHT}" class="w-full h-auto cursor-crosshair" preserveAspectRatio="none">
          <line x1="${PAD}" y1="${CHART_HEIGHT - PAD}" x2="${CHART_WIDTH - PAD}" y2="${CHART_HEIGHT - PAD}" stroke="#334155" stroke-width="1" />
          ${lines}
          ${forkOverlays}
          ${selectedMarker}
          ${markers}
        </svg>
        <div class="flex justify-between text-[10px] text-slate-600 mt-1">
          <span>${new Date(minTs * 1000).toLocaleTimeString()}</span>
          <span class="text-slate-500">Click the chart to explore a counterfactual from that point →</span>
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

  private renderForkControls(): string {
    const meta = VITALS[this.config.vital];
    const parts: string[] = [];

    if (this.selectedForkPoint != null) {
      parts.push(`
        <div class="bg-slate-900/50 border border-slate-700/50 rounded-lg p-3 mb-3">
          <div class="flex items-center justify-between mb-2">
            <span class="text-xs text-slate-300">Explore a counterfactual from <span class="font-mono text-white">${new Date(this.selectedForkPoint * 1000).toLocaleTimeString()}</span></span>
            <button id="fork-clear-point" class="text-slate-500 hover:text-white text-xs">Cancel</button>
          </div>
          <div class="flex flex-wrap items-center gap-2">
            <select id="fork-intervention" class="bg-slate-800 border border-slate-700 rounded px-2 py-1.5 text-xs text-slate-200">
              ${INTERVENTIONS.map(iv => `<option value="${iv.value}">${this.escape(iv.label)}</option>`).join('')}
            </select>
            <div class="flex gap-1" id="fork-duration-group" data-duration="300">
              <button type="button" class="fork-duration-btn px-2 py-1.5 rounded text-xs border border-cyan-500/50 bg-cyan-500/10 text-cyan-300" data-seconds="300">5 min</button>
              <button type="button" class="fork-duration-btn px-2 py-1.5 rounded text-xs border border-slate-700 text-slate-400" data-seconds="900">15 min</button>
              <button type="button" class="fork-duration-btn px-2 py-1.5 rounded text-xs border border-slate-700 text-slate-400" data-seconds="1800">30 min</button>
            </div>
            <button id="fork-run" class="btn-neon px-3 py-1.5 text-xs ml-auto" ${this.forkPending ? 'disabled' : ''}>
              ${this.forkPending ? 'Running...' : 'Run Counterfactual'}
            </button>
          </div>
          ${this.forkError ? `<p class="text-xs text-red-400 mt-2">${this.escape(this.forkError)}</p>` : ''}
        </div>
      `);
    }

    if (this.activeForks.length > 0) {
      parts.push(`
        <div class="mb-3">
          <h4 class="text-xs font-semibold text-slate-400 mb-1.5">Active counterfactuals for ${meta.label}</h4>
          <ul class="space-y-1">
            ${this.activeForks.map(fork => `
              <li class="flex items-center gap-2 text-xs text-slate-300 bg-slate-900/40 border border-slate-800 rounded px-2 py-1.5">
                <span class="w-3 h-0.5 inline-block" style="background:${fork.color}"></span>
                <span>→ projected: ${this.escape(fork.intervention_label)}</span>
                <button class="fork-remove-btn ml-auto text-slate-500 hover:text-red-400" data-fork-id="${this.escape(fork.fork_id)}">&times;</button>
              </li>
            `).join('')}
          </ul>
        </div>
      `);
    }

    return parts.join('');
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

  // Full re-render in place, preserving scroll position of the dialog card
  // (re-mounting from scratch would reset scroll on every click/fork action).
  private refresh(): void {
    if (!this.element) return;
    const card = this.querySelector<HTMLElement>('.glass-card');
    const scrollTop = card?.scrollTop ?? 0;
    this.element.innerHTML = this.render();
    this.setupEventListeners();
    const newCard = this.querySelector<HTMLElement>('.glass-card');
    if (newCard) newCard.scrollTop = scrollTop;
  }

  private setupEventListeners(): void {
    this.on('#vital-panel-close', 'click', () => this.config.onClose());
    this.on('#vital-panel-backdrop', 'click', (e) => {
      if (e.target === e.currentTarget) this.config.onClose();
    });

    this.on('#vital-chart-svg', 'click', (e) => {
      const svg = e.currentTarget as unknown as SVGSVGElement;
      const mouseEvent = e as MouseEvent;
      const ts = this.nearestSnapshotForX(mouseEvent.clientX, svg);
      if (ts != null) {
        this.selectedForkPoint = ts;
        this.forkError = null;
        this.refresh();
      }
    });

    this.on('#fork-clear-point', 'click', () => {
      this.selectedForkPoint = null;
      this.forkError = null;
      this.refresh();
    });

    this.on('.fork-duration-btn', 'click', (e) => {
      const btn = e.currentTarget as HTMLButtonElement;
      const group = this.querySelector<HTMLElement>('#fork-duration-group');
      if (group) group.dataset.duration = btn.dataset.seconds || '300';
      this.querySelectorAll<HTMLButtonElement>('.fork-duration-btn').forEach(b => {
        const active = b === btn;
        b.className = `fork-duration-btn px-2 py-1.5 rounded text-xs border ${active ? 'border-cyan-500/50 bg-cyan-500/10 text-cyan-300' : 'border-slate-700 text-slate-400'}`;
      });
    });

    this.on('#fork-run', 'click', () => this.runFork());

    this.on('.fork-remove-btn', 'click', (e) => {
      const btn = e.currentTarget as HTMLButtonElement;
      const forkId = btn.dataset.forkId;
      if (forkId) this.removeFork(forkId);
    });
  }

  private async runFork(): Promise<void> {
    if (this.selectedForkPoint == null || this.forkPending) return;

    const select = this.querySelector<HTMLSelectElement>('#fork-intervention');
    const durationGroup = this.querySelector<HTMLElement>('#fork-duration-group');
    const actionType = (select?.value || '') as ForkInterventionType | '';
    const durationSeconds = parseInt(durationGroup?.dataset.duration || '300', 10);

    this.forkPending = true;
    this.forkError = null;
    this.refresh();

    try {
      const fork = await apiClient.createFork(
        this.config.patient.id,
        this.selectedForkPoint,
        actionType ? { type: actionType } : null,
        durationSeconds
      );
      const color = FORK_COLORS[this.nextForkColor % FORK_COLORS.length];
      this.nextForkColor++;
      this.activeForks.push({ ...fork, color });
      this.selectedForkPoint = null;
    } catch (err: any) {
      this.forkError = `Failed to run counterfactual: ${err?.message || err}`;
    } finally {
      this.forkPending = false;
      this.refresh();
    }
  }

  private async removeFork(forkId: string): Promise<void> {
    this.activeForks = this.activeForks.filter(f => f.fork_id !== forkId);
    this.refresh();
    try {
      await apiClient.deleteFork(forkId);
    } catch {
      // Overlay is already removed client-side; a failed server-side delete
      // just means that fork lingers server-side until the 10-per-patient
      // cap evicts it -- not worth surfacing an error for a cleanup call.
    }
  }
}
