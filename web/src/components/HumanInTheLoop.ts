import { Component } from './component';
import type { HumanAction } from '@api/types';
import { apiClient } from '@api/client';
import { showToast } from '@utils/ui-helpers';

export interface HumanInTheLoopConfig {
  sessionId: string;
  events: HumanAction[];
  canAnnotate: boolean;
  canEscalate: boolean;
  canFlag: boolean;
}

// Acknowledge / annotate / flag / escalate, all recorded via the same real,
// already-working audit endpoint ClinicalDashboard's alert acknowledge and
// override buttons use (POST /api/acmk/environment/human-event) -- this is
// a general-purpose version of that plus the event history for the
// session. "Freeze" lives in TemporalControls instead (it's a control-plane
// action, not an audit note), so it isn't duplicated here.
export class HumanInTheLoopComponent extends Component {
  private config: HumanInTheLoopConfig;

  constructor(config: HumanInTheLoopConfig) {
    super();
    this.config = config;
  }

  render(): string {
    const { canAnnotate, canEscalate, canFlag } = this.config;

    return `
      <div class="glass-card p-6">
        <h3 class="text-lg font-semibold text-white mb-1">Human-in-the-Loop</h3>
        <p class="text-xs text-slate-500 mb-4">Acknowledge, annotate, flag, or escalate -- every action is timestamped and attributed to you</p>

        <div class="flex flex-wrap gap-2 mb-4">
          <button id="btn-hitl-ack" class="text-xs font-medium px-3 py-1.5 rounded-md border border-white/10 text-slate-300 hover:bg-slate-900/60 transition-colors">✓ Acknowledge</button>
          ${canAnnotate ? `<button id="btn-hitl-show-annotate" class="text-xs font-medium px-3 py-1.5 rounded-md border border-white/10 text-slate-300 hover:bg-slate-900/60 transition-colors">📝 Annotate</button>` : ''}
          ${canFlag ? `<button id="btn-hitl-show-flag" class="text-xs font-medium px-3 py-1.5 rounded-md border border-orange-500/30 text-orange-300 hover:bg-orange-500/10 transition-colors">🚩 Flag Issue</button>` : ''}
          ${canEscalate ? `<button id="btn-hitl-show-escalate" class="text-xs font-medium px-3 py-1.5 rounded-md border border-red-500/30 text-red-300 hover:bg-red-500/10 transition-colors">🔔 Escalate</button>` : ''}
        </div>

        ${canAnnotate ? this.renderInlineForm('annotate', 'Add Annotation', 'e.g. Patient just ambulated, which explains elevated HR', 'Submit Annotation') : ''}
        ${canFlag ? this.renderInlineForm('flag', 'Flag Issue', 'Describe the issue (data quality, system error, missing data, etc.)', 'Submit Flag') : ''}
        ${canEscalate ? this.renderInlineForm('escalate', 'Escalate to Provider', 'Reason for escalation (findings + clinical assessment)', 'Escalate Now') : ''}

        <div id="hitl-history">
          ${this.renderHistory()}
        </div>
      </div>
    `;
  }

  private renderInlineForm(key: string, title: string, placeholder: string, submitLabel: string): string {
    return `
      <div class="hidden bg-slate-900/50 border border-slate-800 rounded-lg p-4 mb-4" id="hitl-panel-${key}">
        <h4 class="text-sm font-semibold text-white mb-2">${title}</h4>
        <textarea id="hitl-text-${key}" rows="3" placeholder="${placeholder}"
                  class="w-full px-3 py-2 rounded-lg bg-slate-950/50 border border-white/10 text-white placeholder-slate-500 text-sm focus:outline-none focus:border-cyan-500/50"></textarea>
        <div class="flex gap-2 mt-3">
          <button id="hitl-cancel-${key}" class="text-xs px-3 py-1.5 rounded border border-white/10 text-slate-400 hover:bg-slate-900/50 transition-colors">Cancel</button>
          <button id="hitl-submit-${key}" class="text-xs px-3 py-1.5 rounded bg-gradient-to-r from-cyan-500 to-blue-600 text-white font-medium hover:opacity-90 transition-opacity">${submitLabel}</button>
        </div>
      </div>
    `;
  }

  private renderHistory(): string {
    const { events } = this.config;
    if (events.length === 0) {
      return '<p class="text-xs text-slate-500">No human actions recorded yet this session.</p>';
    }
    return `
      <h4 class="text-sm font-semibold text-white mb-2">Recent Actions (${events.length})</h4>
      <div class="space-y-1.5 max-h-56 overflow-y-auto">
        ${events.slice().reverse().map(e => `
          <div class="text-xs bg-slate-900/40 border border-slate-800 rounded px-3 py-2">
            <div class="flex items-center justify-between mb-0.5">
              <span class="font-semibold text-cyan-400 uppercase tracking-wide text-[10px]">${e.event_type}</span>
              <span class="text-slate-500 font-mono">${new Date(e.timestamp).toLocaleTimeString()}</span>
            </div>
            <div class="text-slate-400">${e.user_id} — ${e.content || e.scope}</div>
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
    this.element = null;
  }

  updateHistory(events: HumanAction[]): void {
    this.config.events = events;
    const el = this.querySelector<HTMLElement>('#hitl-history');
    if (el) el.innerHTML = this.renderHistory();
  }

  private setupEventListeners(): void {
    this.on('#btn-hitl-ack', 'click', async () => {
      try {
        await this.submit('acknowledge', 'session', 'General session acknowledgment');
        showToast('Acknowledged', 'success');
      } catch (err: any) {
        showToast(`Failed: ${err.message}`, 'error');
      }
    });

    for (const key of ['annotate', 'flag', 'escalate']) {
      this.on(`#btn-hitl-show-${key}`, 'click', () => {
        this.querySelector<HTMLElement>(`#hitl-panel-${key}`)?.classList.remove('hidden');
      });
      this.on(`#hitl-cancel-${key}`, 'click', () => {
        this.querySelector<HTMLElement>(`#hitl-panel-${key}`)?.classList.add('hidden');
      });
      this.on(`#hitl-submit-${key}`, 'click', async () => {
        const textarea = this.querySelector<HTMLTextAreaElement>(`#hitl-text-${key}`);
        const content = textarea?.value.trim();
        if (!content) return;
        const eventType = key === 'flag' ? 'flag_anomaly' : key;
        try {
          await this.submit(eventType, 'session', content);
          if (textarea) textarea.value = '';
          this.querySelector<HTMLElement>(`#hitl-panel-${key}`)?.classList.add('hidden');
          showToast(`${title(key)} recorded`, 'success');
        } catch (err: any) {
          showToast(`Failed: ${err.message}`, 'error');
        }
      });
    }
  }

  private async submit(eventType: string, scope: string, content: string): Promise<void> {
    await apiClient.recordHumanEvent(eventType, scope, content, this.config.sessionId);
    const events = await apiClient.getHumanEvents(this.config.sessionId);
    this.updateHistory(events);
  }
}

function title(key: string): string {
  return key.charAt(0).toUpperCase() + key.slice(1);
}
