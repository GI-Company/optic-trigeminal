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
const BAND_HEIGHT = 18;

// Phase portrait (HR vs. systolic BP state space) -- fixed axis bounds
// rather than auto-scaled to whatever's currently visible, so the "shape"
// of a trajectory means the same thing every time you look at it: a tiny
// wiggle for a stable patient stays visually tiny, a real crisis swing
// stays visually large. Bounds are wide enough to comfortably cover what
// this simulator's tuned physiology actually produces (see
// src/clinical/ode_physiology.cpp's crisis/drug ceilings), not the
// absolute hard clamps in apply_hard_limits -- an excursion beyond this
// box would already be off the edge of clinically-plausible for this model.
const PHASE_SIZE = 220;
const PHASE_PAD = 34;
const PHASE_BP_MIN = 40, PHASE_BP_MAX = 220;
const PHASE_HR_MIN = 30, PHASE_HR_MAX = 200;
// Illustrative adult "normal" reference box -- a rough teaching anchor
// (roughly HR 60-100, SBP 90-140), not a clinical cutoff or NEWS2/qSOFA
// threshold in its own right.
const PHASE_NORMAL_BP = [90, 140];
const PHASE_NORMAL_HR = [60, 100];

// Causal attribution band (CCPC layer 1) -- color + label per
// dominant_physiology_driver id (src/clinical/ode_physiology.cpp).
// Deliberately a different, more saturated palette than FORK_COLORS/VITALS
// above so a band segment is never confused with a vital line or a fork
// overlay. "baseline" is desaturated slate -- a quiet strip meaning
// nothing hidden is meaningfully off right now.
const DRIVER_META: Record<string, { label: string; color: string }> = {
  infection: { label: 'Infection', color: '#dc2626' },
  hypovolemia: { label: 'Low volume', color: '#ea580c' },
  vasodilation: { label: 'Vasodilation', color: '#ca8a04' },
  hypoxia: { label: 'Hypoxia', color: '#0891b2' },
  low_contractility: { label: 'Low contractility', color: '#7c3aed' },
  vasopressor: { label: 'Vasopressor', color: '#db2777' },
  fluid_resuscitation: { label: 'Fluid resuscitation', color: '#0d9488' },
  antipyretic: { label: 'Antipyretic', color: '#4f46e5' },
  baseline: { label: 'Near baseline', color: '#475569' }
};

const SCORE_RIBBON_HEIGHT = 28;
// NEWS2 totals at/above this render at full ribbon height -- 20 is the
// real theoretical max, but this simulator's tuned physiology rarely
// pushes a single snapshot past single digits, so scaling to the full
// theoretical range would make almost every bar look tiny.
const SCORE_RIBBON_MAX = 10;

// Colors deliberately reuse a tracked vital's own line color where a
// direct one exists (heart_rate/spo2/respiration/temperature), so "this
// ribbon segment is cyan" reads as "heart rate" the same way it does in
// the chart above it. The three NEWS2 parameters with no vitals-chart
// line of their own (systolic BP, supplemental O2, consciousness) get new
// colors not used anywhere else in this file.
const NEWS2_PARAM_META: Record<string, { label: string; color: string }> = {
  heart_rate: { label: 'Heart rate', color: '#22d3ee' },
  spo2: { label: 'Oxygen saturation', color: '#60a5fa' },
  respiration: { label: 'Respiratory rate', color: '#a78bfa' },
  temperature: { label: 'Temperature', color: '#fbbf24' },
  systolic: { label: 'Systolic BP', color: '#f97316' },
  oxygen: { label: 'Supplemental O2', color: '#14b8a6' },
  consciousness: { label: 'Consciousness', color: '#e879f9' },
  none: { label: 'No points scored', color: '#475569' }
};

interface ActiveFork extends PatientFork {
  color: string;
  // The timestamp the fork branched from -- captured client-side from
  // selectedForkPoint at creation time (the same value already sent to
  // POST /api/clinical/fork as from_timestamp) rather than round-tripped
  // back from the server, since the create response doesn't echo it and
  // the value we sent is already authoritative.
  originTimestamp: number;
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
          ${this.renderPhasePortrait()}
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
      return `<polyline points="${points}" fill="none" stroke="${fork.color}" stroke-width="2.5" stroke-dasharray="7,4" opacity="0.95" stroke-linejoin="round" stroke-linecap="round" />`;
    }).join('');

    // Persistent origin marker per active fork -- where it actually branched
    // from, distinct from the transient dashed selectedMarker below (which
    // only exists while still picking a point, and disappears once a fork is
    // run). Solid, in the fork's own color, with a small flag at the top so
    // several forks from different origins stay visually distinguishable.
    const forkOriginMarkers = this.activeForks.map(fork => {
      const x = xOf(fork.originTimestamp).toFixed(1);
      return `<g opacity="0.85">
        <line x1="${x}" y1="${PAD}" x2="${x}" y2="${CHART_HEIGHT - PAD}" stroke="${fork.color}" stroke-width="1.5" />
        <circle cx="${x}" cy="${PAD}" r="3" fill="${fork.color}" />
      </g>`;
    }).join('');

    return `
      <div class="bg-slate-900/50 border border-slate-800 rounded-lg p-3 mb-3">
        <svg id="vital-chart-svg" viewBox="0 0 ${CHART_WIDTH} ${CHART_HEIGHT}" class="w-full h-auto cursor-crosshair" preserveAspectRatio="none">
          <line x1="${PAD}" y1="${CHART_HEIGHT - PAD}" x2="${CHART_WIDTH - PAD}" y2="${CHART_HEIGHT - PAD}" stroke="#334155" stroke-width="1" />
          ${lines}
          ${forkOverlays}
          ${forkOriginMarkers}
          ${selectedMarker}
          ${markers}
        </svg>
        <div class="flex justify-between text-[10px] text-slate-600 mt-1 mb-2">
          <span>${new Date(minTs * 1000).toLocaleTimeString()}</span>
          <span class="text-slate-500">Click the chart to explore a counterfactual from that point →</span>
          <span>${new Date(maxTs * 1000).toLocaleTimeString()}</span>
        </div>
        ${this.renderAttributionBand(minTs, maxTs)}
        ${this.renderScoreRibbon(minTs, maxTs)}
      </div>
    `;
  }

  // Score-contribution ribbon (CCPC layer 2): a small histogram under the
  // attribution band -- bar height is the NEWS2 total at that snapshot,
  // color is whichever single parameter contributed the most points. A
  // different lens than the attribution band above: that one explains the
  // hidden physiology *causing* the vitals, this shows which *observable,
  // scored* parameter is actually moving the number a nurse escalates on --
  // they can genuinely disagree (a patient can be physiologically septic
  // before any single NEWS2 parameter has crossed into scoring territory).
  private renderScoreRibbon(minTs: number, maxTs: number): string {
    const { patient } = this.config;
    const timestamps = patient.snapshot_timestamps;
    const totals = patient.snapshot_news2_total;
    const dominants = patient.snapshot_news2_dominant;
    if (!timestamps || !totals || !dominants || timestamps.length === 0) return '';

    const xOf = (ts: number) => this.xOf(ts, minTs, maxTs);
    const indices = timestamps
      .map((_, i) => i)
      .filter(i => timestamps[i] >= minTs && timestamps[i] <= maxTs);
    if (indices.length === 0) return '';

    const segments = indices.map((idx, pos) => {
      const startTs = timestamps[idx];
      const endTs = pos + 1 < indices.length ? timestamps[indices[pos + 1]] : maxTs;
      const x1 = xOf(startTs);
      const x2 = Math.max(x1 + 1, xOf(endTs));
      const total = totals[idx] ?? 0;
      const dominantId = dominants[idx] || 'none';
      const meta = NEWS2_PARAM_META[dominantId] || NEWS2_PARAM_META.none;
      const frac = Math.min(1, total / SCORE_RIBBON_MAX);
      const barHeight = total === 0 ? 2 : Math.max(3, frac * SCORE_RIBBON_HEIGHT);
      const y = SCORE_RIBBON_HEIGHT - barHeight;
      const label = total === 0 ? 'No NEWS2 points' : `${this.escape(meta.label)}: ${total} pt${total === 1 ? '' : 's'} of NEWS2 total`;
      return `<rect x="${x1.toFixed(1)}" y="${y.toFixed(1)}" width="${(x2 - x1).toFixed(1)}" height="${barHeight.toFixed(1)}" fill="${meta.color}" opacity="${total === 0 ? 0.3 : 0.85}"><title>${label} @ ${new Date(startTs * 1000).toLocaleTimeString()}</title></rect>`;
    }).join('');

    const present = Array.from(new Set(indices.map(i => dominants[i] || 'none')));
    const legend = present.map(id => {
      const meta = NEWS2_PARAM_META[id] || NEWS2_PARAM_META.none;
      return `<span class="inline-flex items-center gap-1"><span class="w-2 h-2 rounded-full inline-block" style="background:${meta.color}"></span>${this.escape(meta.label)}</span>`;
    }).join('');

    return `
      <div class="pt-1 mt-1 border-t border-slate-800/70">
        <div class="flex items-center justify-between mb-1">
          <span class="text-[10px] font-semibold text-slate-400 uppercase tracking-wide">Score Contribution (NEWS2)</span>
          <span class="text-[10px] text-slate-600">Bar height = points scored, color = which parameter</span>
        </div>
        <svg viewBox="0 0 ${CHART_WIDTH} ${SCORE_RIBBON_HEIGHT}" class="w-full" style="height:${SCORE_RIBBON_HEIGHT}px" preserveAspectRatio="none">
          ${segments}
        </svg>
        <div class="flex flex-wrap gap-x-3 gap-y-1 mt-1.5 text-[10px] text-slate-500">${legend}</div>
      </div>
    `;
  }

  // Causal attribution band (CCPC layer 1): a thin strip under the live
  // line showing which single hidden physiology term or active drug best
  // explains the vitals at each point in the visible window -- the "why"
  // layer the fork overlay above doesn't answer on its own. Reuses the
  // same CHART_WIDTH-based x-mapping as the line chart above so it lines
  // up under it exactly (same PAD, same viewBox width).
  private renderAttributionBand(minTs: number, maxTs: number): string {
    const { patient } = this.config;
    const timestamps = patient.snapshot_timestamps;
    const drivers = patient.snapshot_drivers;
    const magnitudes = patient.snapshot_driver_magnitudes;
    if (!timestamps || !drivers || !magnitudes || timestamps.length === 0) return '';

    const xOf = (ts: number) => this.xOf(ts, minTs, maxTs);

    // The snapshot ring (up to 120 ticks) reaches further back than the
    // ~20-sample vitals line it sits under -- filter to what's actually in
    // the currently visible window rather than assuming the two arrays
    // are the same length.
    const indices = timestamps
      .map((_, i) => i)
      .filter(i => timestamps[i] >= minTs && timestamps[i] <= maxTs);
    if (indices.length === 0) return '';

    const segments = indices.map((idx, pos) => {
      const startTs = timestamps[idx];
      const endTs = pos + 1 < indices.length ? timestamps[indices[pos + 1]] : maxTs;
      const x1 = xOf(startTs);
      const x2 = Math.max(x1 + 1, xOf(endTs));
      const driverId = drivers[idx] || 'baseline';
      const meta = DRIVER_META[driverId] || DRIVER_META.baseline;
      const magnitude = magnitudes[idx] ?? 0;
      const opacity = driverId === 'baseline' ? 0.25 : 0.35 + magnitude * 0.55;
      return `<rect x="${x1.toFixed(1)}" y="0" width="${(x2 - x1).toFixed(1)}" height="${BAND_HEIGHT}" fill="${meta.color}" opacity="${opacity.toFixed(2)}"><title>${this.escape(meta.label)} (${Math.round(magnitude * 100)}%) @ ${new Date(startTs * 1000).toLocaleTimeString()}</title></rect>`;
    }).join('');

    // Legend only lists drivers actually present in this window -- never a
    // dead entry for something that isn't currently shown.
    const present = Array.from(new Set(indices.map(i => drivers[i] || 'baseline')));
    const legend = present.map(id => {
      const meta = DRIVER_META[id] || DRIVER_META.baseline;
      return `<span class="inline-flex items-center gap-1"><span class="w-2 h-2 rounded-full inline-block" style="background:${meta.color}"></span>${this.escape(meta.label)}</span>`;
    }).join('');

    return `
      <div class="pt-1 border-t border-slate-800/70">
        <div class="flex items-center justify-between mb-1">
          <span class="text-[10px] font-semibold text-slate-400 uppercase tracking-wide">Causal Attribution</span>
          <span class="text-[10px] text-slate-600">What's driving these vitals -- not a diagnosis</span>
        </div>
        <svg viewBox="0 0 ${CHART_WIDTH} ${BAND_HEIGHT}" class="w-full" style="height:${BAND_HEIGHT}px" preserveAspectRatio="none">
          ${segments}
        </svg>
        <div class="flex flex-wrap gap-x-3 gap-y-1 mt-1.5 text-[10px] text-slate-500">${legend}</div>
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
      const fmt = (v: number) => this.config.vital === 'temp' ? v.toFixed(1) : String(Math.round(v));
      parts.push(`
        <div class="mb-3">
          <h4 class="text-xs font-semibold text-slate-400 mb-1.5">Active counterfactuals for ${meta.label}</h4>
          <ul class="space-y-1">
            ${this.activeForks.map(fork => {
              // Delta summary for the currently-selected vital, start vs end
              // of this fork's own trajectory -- always available (unlike a
              // comparison to "what actually happened," which doesn't exist
              // yet for a future the real patient hasn't lived through) and
              // lets a student read the net effect without eyeballing the chart.
              let delta = '';
              if (fork.trajectory.length > 0) {
                const start = meta.trajectoryValue(fork.trajectory[0]);
                const end = meta.trajectoryValue(fork.trajectory[fork.trajectory.length - 1]);
                const diff = end - start;
                const sign = diff > 0 ? '+' : '';
                delta = `<span class="text-slate-500">${fmt(start)} → ${fmt(end)} ${meta.unit} (${sign}${fmt(diff)})</span>`;
              }
              return `
              <li class="flex items-center gap-2 text-xs text-slate-300 bg-slate-900/40 border border-slate-800 rounded px-2 py-1.5">
                <span class="w-3 h-0.5 inline-block" style="background:${fork.color}"></span>
                <span>→ projected: ${this.escape(fork.intervention_label)}</span>
                ${delta}
                <button class="fork-remove-btn ml-auto text-slate-500 hover:text-red-400" data-fork-id="${this.escape(fork.fork_id)}">&times;</button>
              </li>
            `;
            }).join('')}
          </ul>
        </div>
      `);
    }

    return parts.join('');
  }

  // Phase portrait: the same history plotted as a trajectory through
  // HR-vs-systolic-BP state space instead of against time -- students are
  // taught to read shock staging off exactly this relationship (compensated
  // shock: HR climbs while BP holds; decompensation: both break away
  // together), which a time-series chart makes you infer and this makes
  // directly visible. Reuses the snapshot ring (see renderAttributionBand)
  // for HR/BP since there's no separate rolling BP history array.
  private renderPhasePortrait(): string {
    const { patient } = this.config;
    const timestamps = patient.snapshot_timestamps;
    const hrs = patient.snapshot_hr;
    const bps = patient.snapshot_bp_sys;
    if (!timestamps || !hrs || !bps || timestamps.length === 0) return '';

    const { minTs, maxTs } = this.timeDomain();
    const indices = timestamps
      .map((_, i) => i)
      .filter(i => timestamps[i] >= minTs && timestamps[i] <= maxTs);
    if (indices.length < 2) return '';

    const xOf = (bp: number) => PHASE_PAD + ((bp - PHASE_BP_MIN) / (PHASE_BP_MAX - PHASE_BP_MIN)) * (PHASE_SIZE - 2 * PHASE_PAD);
    const yOf = (hr: number) => PHASE_SIZE - PHASE_PAD - ((hr - PHASE_HR_MIN) / (PHASE_HR_MAX - PHASE_HR_MIN)) * (PHASE_SIZE - 2 * PHASE_PAD);

    // Segments (not one polyline) so each can fade from dim (oldest) to
    // bright (most recent) -- shows direction of travel without needing
    // arrowheads cluttering a small chart.
    const segments = indices.slice(0, -1).map((idx, pos) => {
      const nextIdx = indices[pos + 1];
      const frac = pos / Math.max(1, indices.length - 2);
      const opacity = 0.15 + frac * 0.65;
      return `<line x1="${xOf(bps[idx]).toFixed(1)}" y1="${yOf(hrs[idx]).toFixed(1)}" x2="${xOf(bps[nextIdx]).toFixed(1)}" y2="${yOf(hrs[nextIdx]).toFixed(1)}" stroke="#22d3ee" stroke-width="2" opacity="${opacity.toFixed(2)}" stroke-linecap="round" />`;
    }).join('');

    const lastIdx = indices[indices.length - 1];
    const curX = xOf(bps[lastIdx]);
    const curY = yOf(hrs[lastIdx]);

    // Active forks in the same space, same dashed/per-fork-color treatment
    // as the main chart overlay -- down-sampled the same way for the same
    // jitter-avoidance reason (see renderChart).
    const forkSegments = this.activeForks.map(fork => {
      const pts = fork.trajectory.filter(pt => pt.timestamp >= minTs && pt.timestamp <= maxTs);
      if (pts.length < 2) return '';
      const targetPoints = 30;
      const step = Math.max(1, Math.ceil(pts.length / targetPoints));
      const sampled = pts.filter((_, i) => i % step === 0 || i === pts.length - 1);
      const points = sampled.map(pt => `${xOf(pt.bp_sys).toFixed(1)},${yOf(pt.hr).toFixed(1)}`).join(' ');
      return `<polyline points="${points}" fill="none" stroke="${fork.color}" stroke-width="2" stroke-dasharray="5,3" opacity="0.85" stroke-linejoin="round" stroke-linecap="round" />`;
    }).join('');

    const boxX1 = xOf(PHASE_NORMAL_BP[0]), boxX2 = xOf(PHASE_NORMAL_BP[1]);
    const boxY1 = yOf(PHASE_NORMAL_HR[0]), boxY2 = yOf(PHASE_NORMAL_HR[1]);
    const bpTicks = [60, 100, 140, 180];
    const hrTicks = [40, 80, 120, 160];

    return `
      <div class="bg-slate-900/50 border border-slate-800 rounded-lg p-3 mb-3">
        <div class="flex items-center justify-between mb-2">
          <span class="text-xs font-semibold text-slate-300">Phase Portrait</span>
          <span class="text-[10px] text-slate-600">Heart Rate vs. Systolic BP -- state space, not time</span>
        </div>
        <div class="flex gap-4 items-start">
          <svg viewBox="0 0 ${PHASE_SIZE} ${PHASE_SIZE}" class="flex-shrink-0" style="width:${PHASE_SIZE}px;height:${PHASE_SIZE}px">
            <rect x="${PHASE_PAD}" y="${PHASE_PAD}" width="${PHASE_SIZE - 2 * PHASE_PAD}" height="${PHASE_SIZE - 2 * PHASE_PAD}" fill="none" stroke="#334155" stroke-width="1" />
            <rect x="${boxX1.toFixed(1)}" y="${boxY2.toFixed(1)}" width="${(boxX2 - boxX1).toFixed(1)}" height="${(boxY1 - boxY2).toFixed(1)}" fill="#34d399" opacity="0.08" stroke="#34d399" stroke-width="1" stroke-dasharray="3,2" />
            ${bpTicks.map(t => `<line x1="${xOf(t).toFixed(1)}" y1="${PHASE_SIZE - PHASE_PAD}" x2="${xOf(t).toFixed(1)}" y2="${PHASE_SIZE - PHASE_PAD + 4}" stroke="#475569" stroke-width="1" /><text x="${xOf(t).toFixed(1)}" y="${PHASE_SIZE - PHASE_PAD + 14}" font-size="8" fill="#64748b" text-anchor="middle">${t}</text>`).join('')}
            ${hrTicks.map(t => `<line x1="${PHASE_PAD - 4}" y1="${yOf(t).toFixed(1)}" x2="${PHASE_PAD}" y2="${yOf(t).toFixed(1)}" stroke="#475569" stroke-width="1" /><text x="${(PHASE_PAD - 7).toFixed(1)}" y="${(yOf(t) + 3).toFixed(1)}" font-size="8" fill="#64748b" text-anchor="end">${t}</text>`).join('')}
            ${segments}
            ${forkSegments}
            <circle cx="${curX.toFixed(1)}" cy="${curY.toFixed(1)}" r="4" fill="#22d3ee" stroke="#0f172a" stroke-width="1.5" />
          </svg>
          <div class="flex-1 text-[10px] text-slate-500 space-y-1.5 pt-1">
            <p><span class="text-cyan-400">●</span> current position &nbsp; <span class="text-slate-400">—</span> brightens toward the present</p>
            <p><span class="inline-block w-2.5 h-2.5 rounded-sm border border-dashed border-emerald-500/50 bg-emerald-500/10 align-[-1px]"></span> illustrative normal-adult range (SBP 90-140, HR 60-100) -- a rough teaching anchor, not a clinical cutoff</p>
            <p>X: Systolic BP (mmHg) &nbsp; Y: Heart Rate (bpm)</p>
            <p>Compensated shock often shows HR climbing while BP still holds near the box; decompensation shows both breaking away together.</p>
            ${this.activeForks.length > 0 ? `<p class="text-slate-400">Dashed: active counterfactual forks (color key below).</p>` : ''}
          </div>
        </div>
      </div>
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

    const originTimestamp = this.selectedForkPoint;

    try {
      const fork = await apiClient.createFork(
        this.config.patient.id,
        this.selectedForkPoint,
        actionType ? { type: actionType } : null,
        durationSeconds
      );
      const color = FORK_COLORS[this.nextForkColor % FORK_COLORS.length];
      this.nextForkColor++;
      this.activeForks.push({ ...fork, color, originTimestamp });
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
