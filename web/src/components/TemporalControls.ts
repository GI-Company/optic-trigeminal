import { Component } from './component';
import type { TemporalSnapshot } from '@api/types';
import { apiClient } from '@api/client';
import { showToast } from '@utils/ui-helpers';
import { Modal } from './Modal';

export interface TemporalControlsConfig {
  sessionId: string;
  snapshots: TemporalSnapshot[];
}

// Real pause/resume/freeze/replay against the ACMK-OT control plane
// (src/kernel/acmk_planes.h: ControlPlane), not simulated buttons -- every
// action requires a stated reason, which the server records into the same
// hash-chained audit trail as chart notes and alert overrides.
export class TemporalControlsComponent extends Component {
  private config: TemporalControlsConfig;
  private activeModal: Modal | null = null;

  constructor(config: TemporalControlsConfig) {
    super();
    this.config = config;
  }

  render(): string {
    return `
      <div class="glass-card p-6">
        <h3 class="text-lg font-semibold text-white mb-1">Temporal Controls</h3>
        <p class="text-xs text-slate-500 mb-4">Pause, resume, freeze, or replay this session's reasoning (ACMK-OT control plane)</p>

        <div class="flex flex-wrap gap-2 mb-5">
          <button id="btn-tc-pause" class="text-xs font-medium px-3 py-1.5 rounded-md border border-white/10 text-slate-300 hover:bg-slate-900/60 transition-colors">⏸️ Pause</button>
          <button id="btn-tc-resume" class="text-xs font-medium px-3 py-1.5 rounded-md border border-white/10 text-slate-300 hover:bg-slate-900/60 transition-colors">▶️ Resume</button>
          <button id="btn-tc-freeze" class="text-xs font-medium px-3 py-1.5 rounded-md border border-amber-500/30 text-amber-300 hover:bg-amber-500/10 transition-colors">🔒 Freeze</button>
        </div>

        <div id="tc-timeline">
          ${this.renderTimeline()}
        </div>
      </div>
    `;
  }

  private renderTimeline(): string {
    const { snapshots } = this.config;
    if (snapshots.length === 0) {
      return '<p class="text-sm text-slate-500">No snapshots yet. Generate an SBAR to record one.</p>';
    }

    return `
      <h4 class="text-sm font-semibold text-white mb-2">Snapshots (${snapshots.length})</h4>
      <div class="space-y-1.5 max-h-64 overflow-y-auto">
        ${snapshots.slice().reverse().map(s => `
          <div class="flex items-center justify-between gap-3 rounded-lg border border-slate-800 bg-slate-900/40 px-3 py-2">
            <div class="min-w-0">
              <div class="text-xs text-slate-300">${new Date(s.timestamp).toLocaleTimeString()}</div>
              <code class="text-[11px] text-slate-500">${s.state_hash.substring(0, 12)}…</code>
            </div>
            <button class="btn-tc-rollback text-[11px] font-medium px-2 py-1 rounded border border-cyan-500/30 text-cyan-300 hover:bg-cyan-500/10 transition-colors shrink-0"
                    data-timestamp="${new Date(s.timestamp).getTime() / 1000}">
              ↩️ Replay from here
            </button>
          </div>
        `).join('')}
      </div>
    `;
  }

  mount(container: HTMLElement): void {
    container.innerHTML = this.render();
    this.element = container;
    this.setupEventListeners();
  }

  unmount(): void {
    this.activeModal?.unmount();
    this.activeModal = null;
    this.element = null;
  }

  // Called by the parent (ReasoningTrace.ts) after a fresh snapshot fetch,
  // e.g. following a new SBAR generation -- re-renders just the timeline,
  // not the whole card, so the pause/resume/freeze buttons above it don't flash.
  updateSnapshots(snapshots: TemporalSnapshot[]): void {
    this.config.snapshots = snapshots;
    const el = this.querySelector<HTMLElement>('#tc-timeline');
    if (el) {
      el.innerHTML = this.renderTimeline();
      this.attachTimelineListeners();
    }
  }

  private setupEventListeners(): void {
    this.on('#btn-tc-pause', 'click', () => this.promptAndRun('Pause Session', (reason) => apiClient.controlPause(this.config.sessionId, reason), 'Session paused'));
    this.on('#btn-tc-resume', 'click', () => this.promptAndRun('Resume Session', (reason) => apiClient.controlResume(this.config.sessionId, reason), 'Session resumed'));
    this.on('#btn-tc-freeze', 'click', () => this.promptAndRun('Freeze Session', (reason) => apiClient.controlFreeze(this.config.sessionId, reason), 'Session frozen'));
    this.attachTimelineListeners();
  }

  private attachTimelineListeners(): void {
    this.on('.btn-tc-rollback', 'click', (e) => {
      const btn = e.currentTarget as HTMLButtonElement;
      const fromTimestamp = parseFloat(btn.dataset.timestamp!);
      this.promptAndRun(
        'Replay From Snapshot',
        (reason) => apiClient.controlReplay(this.config.sessionId, fromTimestamp, 1.0, reason),
        'Replay started'
      );
    });
  }

  private promptAndRun(title: string, action: (reason: string) => Promise<{ status: string }>, successMessage: string): void {
    const root = document.getElementById('modal-root');
    if (!root) return;
    this.activeModal = new Modal({
      title,
      submitLabel: 'Confirm',
      fields: [
        { id: 'reason', label: 'Reason (recorded to the audit trail)', type: 'textarea', placeholder: 'e.g. Reviewing prior reasoning for training purposes', required: true }
      ],
      onCancel: () => { this.activeModal?.unmount(); this.activeModal = null; },
      onSubmit: async (values) => {
        if (!values.reason) {
          this.activeModal?.showError('A reason is required.');
          return;
        }
        try {
          await action(values.reason);
          this.activeModal?.unmount();
          this.activeModal = null;
          showToast(successMessage, 'success');
        } catch (err: any) {
          this.activeModal?.showError(err.message || 'Action failed');
        }
      }
    });
    this.activeModal.mount(root);
  }
}
