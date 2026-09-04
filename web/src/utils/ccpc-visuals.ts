// Shared color/label vocabulary for CCPC (Causal Counterfactual Patient
// Charts) visual layers -- single source of truth so a NEWS2 parameter or
// physiology driver maps to the same color everywhere it appears (the
// per-patient VitalHistoryPanel modal and the multi-patient dashboard
// gestalt view), rather than two independently-maintained copies drifting
// apart from each other over time.

// Colors reuse a tracked vital's own line color where a direct one exists
// (heart_rate/spo2/respiration/temperature) so "this is cyan" reads as
// "heart rate" the same way in every view. The three NEWS2 parameters with
// no vitals-chart line of their own (systolic BP, supplemental O2,
// consciousness) get colors not used anywhere else in this vocabulary.
export const NEWS2_PARAM_META: Record<string, { label: string; color: string }> = {
  heart_rate: { label: 'Heart rate', color: '#22d3ee' },
  spo2: { label: 'Oxygen saturation', color: '#60a5fa' },
  respiration: { label: 'Respiratory rate', color: '#a78bfa' },
  temperature: { label: 'Temperature', color: '#fbbf24' },
  systolic: { label: 'Systolic BP', color: '#f97316' },
  oxygen: { label: 'Supplemental O2', color: '#14b8a6' },
  consciousness: { label: 'Consciousness', color: '#e879f9' },
  none: { label: 'No points scored', color: '#475569' }
};

// Causal attribution band driver vocabulary (see dominant_physiology_driver
// in src/clinical/ode_physiology.cpp) -- deliberately a different, more
// saturated palette than NEWS2_PARAM_META/FORK_COLORS/VITALS above so a
// band segment is never confused with a vital line or a fork overlay.
export const DRIVER_META: Record<string, { label: string; color: string }> = {
  infection: { label: 'Infection', color: '#dc2626' },
  hypovolemia: { label: 'Low volume', color: '#ea580c' },
  vasodilation: { label: 'Vasodilation', color: '#ca8a04' },
  hypoxia: { label: 'Hypoxia', color: '#0891b2' },
  low_contractility: { label: 'Low contractility', color: '#7c3aed' },
  vasopressor: { label: 'Vasopressor', color: '#db2777' },
  fluid_resuscitation: { label: 'Fluid resuscitation', color: '#0d9488' },
  antipyretic: { label: 'Antipyretic', color: '#4f46e5' },
  epinephrine: { label: 'Epinephrine', color: '#e11d48' },
  insulin: { label: 'Insulin', color: '#0ea5e9' },
  metabolic_acidosis: { label: 'Metabolic acidosis', color: '#b45309' },
  baseline: { label: 'Near baseline', color: '#475569' }
};

// Trend direction over a recent window of NEWS2 totals -- compares the mean
// of the first third of the window against the mean of the last third
// (not just first-point-vs-last-point, which a single noisy sample could
// flip) against a small deadband so score noise at a stable baseline
// doesn't read as a trend. Needs at least 3 points to say anything.
export type Trend = 'worsening' | 'improving' | 'stable' | 'unknown';

export function news2Trend(totals: number[]): Trend {
  if (!totals || totals.length < 3) return 'unknown';
  const third = Math.max(1, Math.floor(totals.length / 3));
  const first = totals.slice(0, third);
  const last = totals.slice(-third);
  const mean = (arr: number[]) => arr.reduce((a, b) => a + b, 0) / arr.length;
  const delta = mean(last) - mean(first);
  if (delta >= 0.75) return 'worsening';
  if (delta <= -0.75) return 'improving';
  return 'stable';
}

export const TREND_META: Record<Trend, { arrow: string; color: string; label: string }> = {
  worsening: { arrow: '↑', color: 'text-red-400', label: 'Trending up' },
  improving: { arrow: '↓', color: 'text-emerald-400', label: 'Trending down' },
  stable: { arrow: '→', color: 'text-slate-400', label: 'Stable' },
  unknown: { arrow: '·', color: 'text-slate-600', label: 'Not enough data yet' }
};
