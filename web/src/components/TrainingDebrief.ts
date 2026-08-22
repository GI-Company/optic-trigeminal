import { Component } from './component';
import type { TrainingReport } from '@api/types';

export interface TrainingDebriefConfig {
  report: TrainingReport;
  onDone: () => void;
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

          <div class="flex justify-end">
            <button id="btn-debrief-done" class="btn-neon px-6 py-2.5 text-sm">Return to Dashboard</button>
          </div>
        </div>
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
  }

  unmount(): void {
    this.element = null;
  }
}
