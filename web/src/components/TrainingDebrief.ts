import { Component } from './component';
import type { TrainingReport } from '@api/types';

export interface TrainingDebriefConfig {
  report: TrainingReport;
  onDone: () => void;
  onGenerateNoteDraft: (sessionId: string) => Promise<string>;
  onSignNote: (sessionId: string, content: string, wasEdited: boolean) => Promise<string>;
}

// The debrief -- what a nurse actually sees when a scenario ends. Until now
// ending a session just showed a toast ("Training session ended") and
// bounced straight back to the dashboard; the server was already computing
// real per-action correctness, missed critical windows, and a full
// transcript (see handle_training_end in src/server/http_server.cpp) and
// throwing all of it away. Debrief -- reviewing what happened and why -- is
// the step every clinical simulation framework treats as the actual point
// of the exercise; this is the screen that was missing.
export class TrainingDebrief extends Component {
  private config: TrainingDebriefConfig;
  private noteContent: string = '';
  private originalDraft: string | null = null;
  private loadingDraft = false;
  private signedAt: string | null = null;
  private signing = false;

  constructor(config: TrainingDebriefConfig) {
    super();
    this.config = config;
  }

  render(): string {
    const { report } = this.config;
    const scorePercent = Math.round(report.final_score * 100);
    const scoreColor = scorePercent >= 80 ? 'text-green-400' : scorePercent >= 50 ? 'text-amber-400' : 'text-red-400';
    const scoreRing = scorePercent >= 80 ? 'border-green-500/40 bg-green-500/10' : scorePercent >= 50 ? 'border-amber-500/40 bg-amber-500/10' : 'border-red-500/40 bg-red-500/10';

    return `
      <div class="min-h-screen bg-slate-950 text-slate-100 p-6 animate-fade-in-up">
        <div class="max-w-5xl mx-auto space-y-6">
          <div>
            <div class="text-xs font-medium uppercase tracking-wider text-slate-500 mb-1">Debrief</div>
            <h1 class="text-2xl font-bold text-white">${this.escape(report.scenario)}</h1>
            <p class="text-sm text-slate-400 mt-1">${this.escape(report.status)} &middot; ${this.formatDuration(report.duration_ms)}</p>
          </div>

          <div class="glass-card p-6 flex flex-col md:flex-row items-center gap-6">
            <div class="w-28 h-28 rounded-full border-4 ${scoreRing} flex flex-col items-center justify-center shrink-0">
              <div class="text-3xl font-bold ${scoreColor}">${scorePercent}%</div>
              <div class="text-[10px] text-slate-500 uppercase tracking-wider">Score</div>
            </div>
            <div class="flex-1">
              <p class="text-white font-medium">${this.escape(report.performance.assessment)}</p>
              <div class="grid grid-cols-1 sm:grid-cols-3 gap-3 mt-4">
                ${this.statCard('Time to Intervention', report.performance.time_to_intervention)}
                ${this.statCard('Interventions', report.performance.intervention_selection)}
                ${this.statCard('Escalation', report.performance.escalation)}
              </div>
            </div>
          </div>

          <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
            <div class="glass-card p-6">
              <h2 class="text-sm font-semibold text-white mb-3 flex items-center gap-2">
                <span class="text-green-400">✓</span> What Went Well
              </h2>
              ${report.learning_summary.length === 0
                ? '<p class="text-xs text-slate-500">No correct interventions recorded this session.</p>'
                : `<ul class="space-y-2">${report.learning_summary.map(s => `
                    <li class="text-xs text-slate-300 bg-green-500/5 border border-green-500/20 rounded px-3 py-2">${this.escape(s)}</li>
                  `).join('')}</ul>`
              }
            </div>
            <div class="glass-card p-6">
              <h2 class="text-sm font-semibold text-white mb-3 flex items-center gap-2">
                <span class="text-amber-400">!</span> Areas to Review
              </h2>
              ${report.improvement_areas.length === 0
                ? '<p class="text-xs text-slate-500">No missed windows or incorrect actions -- clean run.</p>'
                : `<ul class="space-y-2">${report.improvement_areas.map(s => `
                    <li class="text-xs text-slate-300 bg-amber-500/5 border border-amber-500/20 rounded px-3 py-2">${this.escape(s)}</li>
                  `).join('')}</ul>`
              }
            </div>
          </div>

          <div class="glass-card p-6">
            <h2 class="text-sm font-semibold text-white mb-3">Action Transcript</h2>
            ${report.transcript.length === 0
              ? '<p class="text-xs text-slate-500">No actions were taken this session.</p>'
              : `<div class="space-y-1.5 max-h-72 overflow-y-auto">
                  ${report.transcript.map(t => `
                    <div class="flex items-center justify-between gap-3 rounded-lg border border-slate-800 bg-slate-900/40 px-3 py-2">
                      <span class="text-xs font-mono text-slate-500 w-14 shrink-0">${this.escape(t.time)}</span>
                      <span class="text-xs text-slate-300 flex-1">${this.escape(t.action)}</span>
                      <span class="text-xs font-semibold ${t.score >= 0 ? 'text-green-400' : 'text-red-400'} shrink-0">${t.score >= 0 ? '+' : ''}${(t.score * 100).toFixed(0)}%</span>
                    </div>
                  `).join('')}
                </div>`
            }
          </div>

          <div id="note-panel">
            ${this.renderNotePanel()}
          </div>

          <div class="flex justify-end">
            <button id="btn-debrief-done" class="btn-neon px-6 py-2.5 text-sm">Return to Dashboard</button>
          </div>
        </div>
      </div>
    `;
  }

  private renderNotePanel(): string {
    return `
      <div class="glass-card p-6">
        <div class="flex items-center justify-between mb-1">
          <h2 class="text-sm font-semibold text-white">Training Note</h2>
          ${!this.signedAt ? `
            <button id="btn-generate-draft" ${this.loadingDraft ? 'disabled' : ''} class="text-xs text-cyan-400 hover:text-cyan-300 disabled:opacity-50">
              ${this.loadingDraft ? 'Generating...' : 'Generate AI Draft'}
            </button>
          ` : ''}
        </div>
        <p class="text-xs text-slate-500 mb-3">
          ${this.signedAt
            ? `Signed at ${this.escape(this.signedAt)}. This is a simulation training reflection, not a clinical record.`
            : 'AI drafts a reflection note from this session\'s real event log. Review and edit before signing -- nothing is submitted until you sign.'}
        </p>
        <textarea id="note-textarea" rows="10"
          class="w-full px-3 py-2 rounded-lg bg-slate-900/50 border border-white/10 text-white placeholder-slate-500 text-xs font-mono focus:outline-none focus:border-cyan-500/50 disabled:opacity-70"
          placeholder="Click &quot;Generate AI Draft&quot; above, or write your own note."
          ${this.signedAt ? 'disabled' : ''}>${this.escape(this.noteContent)}</textarea>
        ${!this.signedAt ? `
          <div class="flex justify-end mt-3">
            <button id="btn-sign-note" ${this.signing ? 'disabled' : ''} class="btn-neon px-5 py-2 text-sm disabled:opacity-50">
              ${this.signing ? 'Submitting...' : 'Sign & Submit Note'}
            </button>
          </div>
        ` : ''}
      </div>
    `;
  }

  private statCard(label: string, value: string): string {
    return `
      <div class="bg-slate-900/50 border border-slate-800 rounded-lg p-3">
        <div class="text-[10px] text-slate-500 uppercase tracking-wider mb-1">${label}</div>
        <div class="text-sm text-slate-200">${this.escape(value)}</div>
      </div>
    `;
  }

  private formatDuration(ms: number): string {
    const totalSeconds = Math.floor(ms / 1000);
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = totalSeconds % 60;
    return `${minutes}m ${seconds}s`;
  }

  private escape(s: string): string {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  mount(container: HTMLElement): void {
    container.innerHTML = this.render();
    this.element = container;
    this.on('#btn-debrief-done', 'click', () => this.config.onDone());
    this.attachNoteListeners();
  }

  unmount(): void {
    this.element = null;
  }

  private attachNoteListeners(): void {
    this.on('#note-textarea', 'input', (e) => {
      this.noteContent = (e.target as HTMLTextAreaElement).value;
    });
    this.on('#btn-generate-draft', 'click', async () => {
      this.loadingDraft = true;
      this.refreshNotePanel();
      try {
        const draft = await this.config.onGenerateNoteDraft(this.config.report.training_session_id);
        this.noteContent = draft;
        this.originalDraft = draft;
      } catch {
        // onGenerateNoteDraft's caller (main-refactored.ts) already
        // surfaces a toast on failure -- nothing more to do here.
      }
      this.loadingDraft = false;
      this.refreshNotePanel();
    });
    this.on('#btn-sign-note', 'click', async () => {
      const content = this.noteContent.trim();
      if (!content) return;
      this.signing = true;
      this.refreshNotePanel();
      try {
        const wasEdited = this.originalDraft !== null && this.originalDraft !== content;
        this.signedAt = await this.config.onSignNote(this.config.report.training_session_id, content, wasEdited);
      } catch {
        // Caller surfaces the error toast; stay in the editable state so
        // the nurse doesn't lose what they wrote.
      }
      this.signing = false;
      this.refreshNotePanel();
    });
  }

  private refreshNotePanel(): void {
    const el = this.element?.querySelector('#note-panel');
    if (el) {
      el.innerHTML = this.renderNotePanel();
      this.attachNoteListeners();
    }
  }
}
