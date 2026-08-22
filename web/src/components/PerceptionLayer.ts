import { Component } from './component';
import type { PerceptualArtifact } from '@api/types';

export interface PerceptionLayerConfig {
  artifacts: PerceptualArtifact[];
}

// Shows what ACMK-OT actually perceived and hashed while reasoning about a
// patient -- currently always a vitals_snapshot (see handle_scaffold in
// src/server/http_server.cpp), the one real input this system perceives.
export class PerceptionLayerComponent extends Component {
  private config: PerceptionLayerConfig;

  constructor(config: PerceptionLayerConfig) {
    super();
    this.config = config;
  }

  render(): string {
    const { artifacts } = this.config;

    if (artifacts.length === 0) {
      return `
        <div class="glass-card p-6">
          <h3 class="text-lg font-semibold text-white mb-1">Perception Layer</h3>
          <p class="text-xs text-slate-500 mb-4">What ACMK-OT perceived while reasoning about this patient</p>
          <p class="text-sm text-slate-500">No perceptual artifacts recorded yet. Generate an SBAR to record one.</p>
        </div>
      `;
    }

    return `
      <div class="glass-card p-6">
        <h3 class="text-lg font-semibold text-white mb-1">Perception Layer</h3>
        <p class="text-xs text-slate-500 mb-4">What ACMK-OT perceived while reasoning about this patient</p>
        <div class="grid grid-cols-1 md:grid-cols-2 gap-3">
          ${artifacts.slice().reverse().map(a => this.renderArtifact(a)).join('')}
        </div>
      </div>
    `;
  }

  private renderArtifact(artifact: PerceptualArtifact): string {
    const confColor = artifact.confidence >= 0.8 ? 'text-green-400' : artifact.confidence >= 0.6 ? 'text-amber-400' : 'text-red-400';
    const confPercent = (artifact.confidence * 100).toFixed(0);

    return `
      <div class="bg-slate-900/40 border border-slate-700/50 rounded-lg p-4">
        <div class="flex items-center justify-between mb-2">
          <span class="text-xs font-semibold uppercase tracking-wider text-cyan-400">${this.formatType(artifact.artifact_type)}</span>
          <span class="text-xs text-slate-500 font-mono">${new Date(artifact.timestamp).toLocaleTimeString()}</span>
        </div>
        <div class="text-xs text-slate-500 mb-2">
          Hash: <code class="text-slate-400">${artifact.content_hash.substring(0, 16)}…</code>
        </div>
        <div class="flex items-center gap-2 mb-2">
          <div class="flex-1 h-1.5 bg-slate-800 rounded-full overflow-hidden">
            <div class="h-full bg-current ${confColor}" style="width: ${confPercent}%"></div>
          </div>
          <span class="text-xs font-medium ${confColor}">${confPercent}%</span>
        </div>
        <div class="flex flex-wrap gap-1">
          ${artifact.alignment_metadata.map(m => `
            <span class="text-[10px] px-1.5 py-0.5 rounded bg-slate-800 border border-slate-700 text-slate-400">${m}</span>
          `).join('')}
        </div>
      </div>
    `;
  }

  private formatType(type: string): string {
    return type.split('_').map(w => w.charAt(0).toUpperCase() + w.slice(1)).join(' ');
  }

  mount(container: HTMLElement): void {
    container.innerHTML = this.render();
    this.element = container;
  }

  unmount(): void {
    this.element = null;
  }
}
