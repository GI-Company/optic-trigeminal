import { Component } from './component';
import type { DecisionEnvelope } from '@api/types';

export interface ExplainabilityPanelConfig {
  envelope: DecisionEnvelope | null;
}

// Shows the real decision envelope from the last SBAR generated in this
// session (handle_scaffold in src/server/http_server.cpp) -- which clinical
// findings drove the recommendation (dominant_constraints), which were
// considered but didn't rise to nurse-attention (rejected_alternatives),
// and the confidence spread across all findings. Originally two separate
// components (ExplainabilityPanel + CognitiveDecisionLayer) that both
// rendered the same DecisionEnvelope with heavy overlap; merged into one.
export class ExplainabilityPanelComponent extends Component {
  private config: ExplainabilityPanelConfig;

  constructor(config: ExplainabilityPanelConfig) {
    super();
    this.config = config;
  }

  render(): string {
    const { envelope } = this.config;

    if (!envelope) {
      return `
        <div class="glass-card p-6">
          <h3 class="text-lg font-semibold text-white mb-1">Explainability &amp; Decision</h3>
          <p class="text-xs text-slate-500 mb-4">Rationale, constraint lineage, and confidence for the last decision this session made</p>
          <p class="text-sm text-slate-500">No decision recorded yet. Generate an SBAR to record one.</p>
        </div>
      `;
    }

    return `
      <div class="glass-card p-6" id="explainability-panel">
        <div class="flex items-center justify-between mb-1">
          <h3 class="text-lg font-semibold text-white">Explainability &amp; Decision</h3>
          <div class="flex gap-2">
            <button id="btn-export-explanation-text" class="text-xs px-2 py-1 rounded border border-white/10 text-slate-400 hover:text-white hover:bg-slate-900/50 transition-colors">Export .txt</button>
            <button id="btn-export-explanation-json" class="text-xs px-2 py-1 rounded border border-white/10 text-slate-400 hover:text-white hover:bg-slate-900/50 transition-colors">Export .json</button>
          </div>
        </div>
        <p class="text-xs text-slate-500 mb-4">Rationale, constraint lineage, and confidence for the last decision this session made</p>

        <div class="bg-slate-900/50 border border-slate-800 rounded-lg p-4 mb-4">
          <div class="text-xs uppercase tracking-wider text-slate-500 mb-1">Recommendation</div>
          <div class="text-sm text-slate-200">${this.escape(envelope.final_state)}</div>
        </div>

        <div class="grid grid-cols-1 md:grid-cols-2 gap-4 mb-4">
          <div>
            <h4 class="text-sm font-semibold text-white mb-2">Dominant Findings (${envelope.dominant_constraints.length})</h4>
            ${envelope.dominant_constraints.length === 0
              ? '<p class="text-xs text-slate-500">No findings required attention.</p>'
              : `<ul class="space-y-1.5">${envelope.dominant_constraints.map(c => `
                  <li class="text-xs text-slate-300 bg-red-500/5 border border-red-500/20 rounded px-2 py-1.5">${this.escape(c)}</li>
                `).join('')}</ul>`
            }
          </div>
          <div>
            <h4 class="text-sm font-semibold text-white mb-2">Considered, Not Escalated (${envelope.rejected_alternatives.length})</h4>
            ${envelope.rejected_alternatives.length === 0
              ? '<p class="text-xs text-slate-500">Nothing else was flagged.</p>'
              : `<ul class="space-y-1.5">${envelope.rejected_alternatives.map(alt => `
                  <li class="text-xs text-slate-400 bg-slate-900/40 border border-slate-800 rounded px-2 py-1.5">${this.escape(alt.reason)}</li>
                `).join('')}</ul>`
            }
          </div>
        </div>

        ${this.renderConfidenceBounds(envelope)}
      </div>
    `;
  }

  private renderConfidenceBounds(envelope: DecisionEnvelope): string {
    const { low, high } = envelope.confidence_bounds;
    const spread = high - low;
    let level = 'High Uncertainty';
    let color = 'text-red-400';
    if (envelope.dominant_constraints.length === 0 && envelope.rejected_alternatives.length === 0) {
      return '';
    }
    if (spread < 0.1) { level = 'Very Confident'; color = 'text-green-400'; }
    else if (spread < 0.2) { level = 'Confident'; color = 'text-cyan-400'; }
    else if (spread < 0.4) { level = 'Moderate Confidence'; color = 'text-amber-400'; }

    return `
      <div class="bg-slate-900/50 border border-slate-800 rounded-lg p-4">
        <h4 class="text-sm font-semibold text-white mb-2">Confidence Spread</h4>
        <div class="flex items-center gap-3">
          <span class="text-xs text-slate-500 w-10">${(low * 100).toFixed(0)}%</span>
          <div class="flex-1 h-2 bg-slate-800 rounded-full relative overflow-hidden">
            <div class="absolute h-full bg-gradient-to-r from-red-500 to-cyan-400 rounded-full" style="left: ${(low * 100).toFixed(0)}%; width: ${Math.max(2, spread * 100).toFixed(0)}%;"></div>
          </div>
          <span class="text-xs text-slate-500 w-10 text-right">${(high * 100).toFixed(0)}%</span>
        </div>
        <div class="mt-2 text-xs font-medium ${color}">${level} <span class="text-slate-500 font-normal">(spread ±${(spread * 100).toFixed(0)}%, across every finding this pass considered)</span></div>
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
    this.element = null;
  }

  private setupEventListeners(): void {
    this.on('#btn-export-explanation-text', 'click', () => this.exportAsText());
    this.on('#btn-export-explanation-json', 'click', () => this.exportAsJson());
  }

  private exportAsText(): void {
    const envelope = this.config.envelope;
    if (!envelope) return;

    let text = 'ACMK-OT Explainability Report\n' + '='.repeat(40) + '\n\n';
    text += `RECOMMENDATION\n${'-'.repeat(40)}\n${envelope.final_state}\n\n`;
    text += `DOMINANT FINDINGS\n${'-'.repeat(40)}\n`;
    envelope.dominant_constraints.forEach(c => { text += `• ${c}\n`; });
    text += `\nCONSIDERED, NOT ESCALATED\n${'-'.repeat(40)}\n`;
    envelope.rejected_alternatives.forEach(alt => { text += `• ${alt.reason}\n`; });
    text += `\nCONFIDENCE BOUNDS\n${'-'.repeat(40)}\n`;
    text += `${(envelope.confidence_bounds.low * 100).toFixed(0)}% - ${(envelope.confidence_bounds.high * 100).toFixed(0)}%\n`;

    this.downloadBlob(text, 'text/plain', `acmk-ot-explanation-${Date.now()}.txt`);
  }

  private exportAsJson(): void {
    const envelope = this.config.envelope;
    if (!envelope) return;
    this.downloadBlob(JSON.stringify(envelope, null, 2), 'application/json', `acmk-ot-explanation-${Date.now()}.json`);
  }

  private downloadBlob(content: string, type: string, filename: string): void {
    const blob = new Blob([content], { type });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    a.click();
    URL.revokeObjectURL(url);
  }
}
