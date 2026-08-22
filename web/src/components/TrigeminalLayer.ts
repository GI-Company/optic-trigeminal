import { Component } from './component';
import type { InferenceGraph } from '@api/types';

export interface TrigeminalLayerConfig {
  graph: InferenceGraph;
}

// Each reasoning pass produces one flat set of independent observation
// nodes (ClinicalAnalyzer::analyze_patient has no hierarchical hypothesis
// tree -- see include/clinical_analyzer.h), so edges will typically be
// empty; this shows the real per-node confidence/status instead of a
// fabricated branching diagram.
export class TrigeminalProcessingLayerComponent extends Component {
  private config: TrigeminalLayerConfig;

  constructor(config: TrigeminalLayerConfig) {
    super();
    this.config = config;
  }

  render(): string {
    const { graph } = this.config;

    if (graph.nodes.length === 0) {
      return `
        <div class="glass-card p-6">
          <h3 class="text-lg font-semibold text-white mb-1">Trigeminal Processing Layer</h3>
          <p class="text-xs text-slate-500 mb-4">Reasoning nodes from this session's clinical analysis</p>
          <p class="text-sm text-slate-500">No reasoning nodes recorded yet. Generate an SBAR to record some.</p>
        </div>
      `;
    }

    const active = graph.nodes.filter(n => n.status === 'active');
    const suppressed = graph.nodes.filter(n => n.status === 'suppressed');
    const avgConfidence = active.length > 0 ? active.reduce((s, n) => s + n.confidence, 0) / active.length : 0;

    return `
      <div class="glass-card p-6">
        <h3 class="text-lg font-semibold text-white mb-1">Trigeminal Processing Layer</h3>
        <p class="text-xs text-slate-500 mb-4">Reasoning nodes from this session's clinical analysis</p>

        <div class="grid grid-cols-3 gap-3 mb-4">
          <div class="bg-slate-900/50 rounded-lg p-3 text-center border border-slate-800">
            <div class="text-2xl font-bold text-cyan-400">${active.length}</div>
            <div class="text-[10px] uppercase tracking-wider text-slate-500">Active</div>
          </div>
          <div class="bg-slate-900/50 rounded-lg p-3 text-center border border-slate-800">
            <div class="text-2xl font-bold text-slate-500">${suppressed.length}</div>
            <div class="text-[10px] uppercase tracking-wider text-slate-500">Suppressed</div>
          </div>
          <div class="bg-slate-900/50 rounded-lg p-3 text-center border border-slate-800">
            <div class="text-2xl font-bold text-white">${(avgConfidence * 100).toFixed(0)}%</div>
            <div class="text-[10px] uppercase tracking-wider text-slate-500">Avg Confidence</div>
          </div>
        </div>

        <div class="space-y-2">
          ${graph.nodes.map(n => `
            <div class="flex items-center justify-between gap-3 rounded-lg border px-3 py-2 ${
              n.status === 'suppressed' ? 'border-slate-800 bg-slate-900/30 opacity-60' : 'border-cyan-900/40 bg-cyan-500/5'
            }">
              <div class="min-w-0">
                <div class="text-xs font-mono text-slate-300 truncate">${n.node_id}</div>
                ${n.suppression_reason ? `<div class="text-[11px] text-slate-500">${n.suppression_reason}</div>` : ''}
              </div>
              <div class="flex items-center gap-2 shrink-0">
                <span class="text-[10px] uppercase tracking-wider px-2 py-0.5 rounded ${
                  n.status === 'suppressed' ? 'bg-slate-800 text-slate-500' : 'bg-cyan-500/10 text-cyan-300'
                }">${n.status}</span>
                <span class="text-xs font-semibold text-slate-300">${(n.confidence * 100).toFixed(0)}%</span>
              </div>
            </div>
          `).join('')}
        </div>
      </div>
    `;
  }

  mount(container: HTMLElement): void {
    container.innerHTML = this.render();
    this.element = container;
  }

  unmount(): void {
    this.element = null;
  }
}
