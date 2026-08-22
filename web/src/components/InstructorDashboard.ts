import { Component } from './component';
import { apiClient } from '@api/client';
import { showToast } from '@utils/ui-helpers';
import type { Cohort, CohortDashboard, ImportedCredential, TrainingReport } from '@api/types';

export interface InstructorDashboardConfig {
  currentStaffName: string;
  onSignOut: () => void;
}

// INSTRUCTOR role: manages class cohorts for mass/institutional education
// adoption -- bulk-provisions real, individually-authenticated student
// accounts (each one signs into the same clinical training simulator any
// RN would) and reviews aggregate progress across the cohort. No clinical
// data access at all (see AuthManager::setup_role_permissions -- same
// oversight-only shape as IT). Backed by CohortManager
// (include/cohort_manager.h) and the handle_instructor_* endpoints.
export class InstructorDashboard extends Component {
  private config: InstructorDashboardConfig;
  private cohorts: Cohort[] = [];
  private selectedCohortId: string | null = null;
  private dashboard: CohortDashboard | null = null;
  private loadingCohorts = true;
  private loadingDashboard = false;
  private loadError: string | null = null;
  private showNewCohortForm = false;
  private lastCredentials: ImportedCredential[] | null = null;
  private viewingReport: TrainingReport | null = null;
  private viewingReportSessionId: string | null = null;
  private loadingReport = false;

  constructor(config: InstructorDashboardConfig) {
    super();
    this.config = config;
  }

  render(): string {
    return `
      <div class="min-h-screen bg-slate-950 text-slate-100 p-6 relative overflow-hidden">
        <div class="fixed top-[-10%] left-[-10%] w-[40%] h-[40%] rounded-full bg-emerald-600/10 blur-[120px] pointer-events-none"></div>
        <div class="fixed bottom-[-10%] right-[-10%] w-[40%] h-[40%] rounded-full bg-cyan-600/10 blur-[120px] pointer-events-none"></div>

        <div class="max-w-7xl mx-auto relative z-10 animate-fade-in-up space-y-6">
          ${this.renderHeader()}
          <div class="grid grid-cols-1 lg:grid-cols-3 gap-6">
            <div class="lg:col-span-1" id="cohort-list-panel">
              ${this.renderCohortListPanel()}
            </div>
            <div class="lg:col-span-2" id="cohort-detail-panel">
              ${this.renderCohortDetailPanel()}
            </div>
          </div>
        </div>
      </div>
    `;
  }

  private renderHeader(): string {
    return `
      <header class="glass-panel rounded-2xl p-6 flex justify-between items-center">
        <div class="flex items-center gap-4">
          <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-emerald-500 to-cyan-600 flex items-center justify-center shadow-lg shadow-emerald-500/20">
            <svg class="w-6 h-6 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 14l9-5-9-5-9 5 9 5zm0 0l6.16-3.422a12.083 12.083 0 01.665 6.479A11.952 11.952 0 0012 20.055a11.952 11.952 0 00-6.824-2.998 12.078 12.078 0 01.665-6.479L12 14zm-4 6v-7.5l4-2.222"></path>
            </svg>
          </div>
          <div>
            <h1 class="text-2xl font-bold text-white tracking-tight">OpticTrigeminal <span class="text-xs align-top bg-emerald-500/20 text-emerald-300 px-2 py-0.5 rounded-full ml-1">Instructor</span></h1>
            <p class="text-slate-400 text-sm">Class cohorts &amp; training progress — no clinical patient access</p>
          </div>
        </div>
        <div class="flex items-center gap-6">
          <div class="text-right">
            <div class="text-sm font-semibold text-white">${this.config.currentStaffName}</div>
            <div class="text-xs text-emerald-300 font-medium uppercase tracking-wider">Instructor</div>
          </div>
          <button id="btn-signout" class="btn-outline-neon px-4 py-2 text-sm flex items-center gap-2">
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 16l4-4m0 0l-4-4m4 4H7m6 4v1a3 3 0 01-3 3H6a3 3 0 01-3-3V7a3 3 0 013-3h4a3 3 0 013 3v1"></path>
            </svg>
            Sign Out
          </button>
        </div>
      </header>
    `;
  }

  private renderCohortListPanel(): string {
    return `
      <div class="glass-card p-6">
        <div class="flex items-center justify-between mb-4">
          <h2 class="text-lg font-semibold text-white">Cohorts</h2>
          <button id="btn-new-cohort" class="text-xs text-emerald-300 hover:text-emerald-200">${this.showNewCohortForm ? 'Cancel' : '+ New Cohort'}</button>
        </div>
        ${this.showNewCohortForm ? `
          <form id="new-cohort-form" class="mb-4 space-y-2">
            <input id="new-cohort-name" type="text" required placeholder="e.g. NURS 310 — Fall 2026"
              class="w-full px-3 py-2 rounded-lg bg-slate-900/50 border border-white/10 text-white placeholder-slate-500 text-sm focus:outline-none focus:border-emerald-500/50" />
            <button type="submit" class="w-full px-3 py-2 rounded-lg bg-gradient-to-r from-emerald-500 to-cyan-600 text-white text-sm font-medium hover:opacity-90 transition-opacity">
              Create Cohort
            </button>
          </form>
        ` : ''}
        <div id="cohort-list" class="space-y-2 max-h-[560px] overflow-y-auto pr-1">
          ${this.renderCohortList()}
        </div>
      </div>
    `;
  }

  private renderCohortList(): string {
    if (this.loadingCohorts) {
      return `<div class="text-sm text-slate-500 py-8 text-center">Loading cohorts…</div>`;
    }
    if (this.loadError) {
      return `<div class="text-sm text-red-400 py-8 text-center">${this.esc(this.loadError)}</div>`;
    }
    if (this.cohorts.length === 0) {
      return `<div class="text-sm text-slate-500 py-8 text-center">No cohorts yet. Create one to start importing a class roster.</div>`;
    }
    return this.cohorts.map(c => `
      <button type="button" data-cohort-id="${this.esc(c.cohort_id)}"
        class="cohort-item w-full text-left rounded-lg border px-4 py-3 transition-colors ${
          c.cohort_id === this.selectedCohortId
            ? 'border-emerald-500/60 bg-emerald-500/10'
            : 'border-slate-800 bg-slate-900/40 hover:border-slate-700'
        }">
        <div class="text-sm font-medium text-white">${this.esc(c.name)}</div>
        <div class="text-xs text-slate-500 mt-0.5">${c.student_count} student${c.student_count === 1 ? '' : 's'} · created ${new Date(c.created_at * 1000).toLocaleDateString()}</div>
      </button>
    `).join('');
  }

  private renderCohortDetailPanel(): string {
    if (!this.selectedCohortId) {
      return `
        <div class="glass-card p-6 h-full flex items-center justify-center min-h-[300px]">
          <p class="text-sm text-slate-500">Select a cohort, or create one, to manage its roster and view progress.</p>
        </div>
      `;
    }
    if (this.loadingDashboard) {
      return `<div class="glass-card p-6 min-h-[300px] flex items-center justify-center"><p class="text-sm text-slate-500">Loading cohort…</p></div>`;
    }
    if (!this.dashboard) {
      return `<div class="glass-card p-6 min-h-[300px] flex items-center justify-center"><p class="text-sm text-red-400">Failed to load cohort.</p></div>`;
    }
    return `
      <div class="space-y-6">
        ${this.renderRosterImportPanel()}
        ${this.lastCredentials ? this.renderCredentialsPanel() : ''}
        ${(this.loadingReport || this.viewingReport) ? this.renderReportPanel() : ''}
        ${this.renderStudentTable()}
        ${this.renderMissedInterventionsPanel()}
      </div>
    `;
  }

  private renderReportPanel(): string {
    if (this.loadingReport) {
      return `<div class="glass-card p-6"><p class="text-sm text-slate-500">Loading report…</p></div>`;
    }
    const r = this.viewingReport!;
    return `
      <div class="glass-card p-6 border border-cyan-500/30">
        <div class="flex items-center justify-between mb-1">
          <h3 class="text-sm font-semibold text-cyan-300">${this.esc(r.scenario)} — Full Report</h3>
          <button id="btn-dismiss-report" class="text-slate-400 hover:text-white text-lg leading-none">&times;</button>
        </div>
        <p class="text-xs text-slate-500 mb-4">${this.esc(r.performance.assessment)}</p>
        <div class="grid grid-cols-3 gap-3 mb-4">
          <div class="rounded-lg border border-slate-800 bg-slate-900/40 px-3 py-2">
            <div class="text-[10px] text-slate-500 uppercase tracking-wider">Time to Intervention</div>
            <div class="text-xs text-slate-200 mt-0.5">${this.esc(r.performance.time_to_intervention)}</div>
          </div>
          <div class="rounded-lg border border-slate-800 bg-slate-900/40 px-3 py-2">
            <div class="text-[10px] text-slate-500 uppercase tracking-wider">Interventions</div>
            <div class="text-xs text-slate-200 mt-0.5">${this.esc(r.performance.intervention_selection)}</div>
          </div>
          <div class="rounded-lg border border-slate-800 bg-slate-900/40 px-3 py-2">
            <div class="text-[10px] text-slate-500 uppercase tracking-wider">Escalation</div>
            <div class="text-xs text-slate-200 mt-0.5">${this.esc(r.performance.escalation)}</div>
          </div>
        </div>
        <div class="grid grid-cols-1 sm:grid-cols-2 gap-4 mb-4">
          <div>
            <p class="text-xs font-semibold text-green-300 mb-1.5">What Went Well</p>
            ${r.learning_summary.length === 0
              ? `<p class="text-xs text-slate-500">No correct interventions recorded.</p>`
              : `<ul class="space-y-1">${r.learning_summary.map(x => `<li class="text-xs text-slate-300">• ${this.esc(x)}</li>`).join('')}</ul>`}
          </div>
          <div>
            <p class="text-xs font-semibold text-amber-300 mb-1.5">Areas to Review</p>
            ${r.improvement_areas.length === 0
              ? `<p class="text-xs text-slate-500">None.</p>`
              : `<ul class="space-y-1">${r.improvement_areas.map(x => `<li class="text-xs text-slate-300">• ${this.esc(x)}</li>`).join('')}</ul>`}
          </div>
        </div>
        <p class="text-xs font-semibold text-slate-400 mb-1.5">Action Transcript</p>
        ${r.transcript.length === 0 ? `<p class="text-xs text-slate-500">No actions recorded.</p>` : `
          <div class="space-y-1 max-h-48 overflow-y-auto">
            ${r.transcript.map(t => `
              <div class="flex items-center justify-between text-xs rounded-lg border border-slate-800/60 bg-slate-900/30 px-3 py-1.5">
                <span class="text-slate-500 w-10">${this.esc(t.time)}</span>
                <span class="text-slate-300 flex-1 px-2">${this.esc(t.action)}</span>
                <span class="${t.score >= 0 ? 'text-green-400' : 'text-red-400'}">${t.score >= 0 ? '+' : ''}${(t.score * 100).toFixed(0)}%</span>
              </div>
            `).join('')}
          </div>
        `}
      </div>
    `;
  }

  private renderRosterImportPanel(): string {
    return `
      <div class="glass-card p-6">
        <h2 class="text-lg font-semibold text-white mb-1">${this.esc(this.dashboard!.cohort.name)}</h2>
        <p class="text-xs text-slate-500 mb-4">Paste one student per line: <code>Full Name, email or student ID</code> (the second field is optional).</p>
        <form id="import-roster-form" class="space-y-2">
          <textarea id="roster-textarea" rows="4" placeholder="Jane Doe, jane.doe@school.edu&#10;John Smith, john.smith@school.edu"
            class="w-full px-3 py-2 rounded-lg bg-slate-900/50 border border-white/10 text-white placeholder-slate-500 text-sm font-mono focus:outline-none focus:border-emerald-500/50"></textarea>
          <button type="submit" id="btn-import-roster" class="px-4 py-2 rounded-lg bg-gradient-to-r from-emerald-500 to-cyan-600 text-white text-sm font-medium hover:opacity-90 transition-opacity">
            Import Students
          </button>
        </form>
      </div>
    `;
  }

  private renderCredentialsPanel(): string {
    const creds = this.lastCredentials!;
    return `
      <div class="glass-card p-6 border border-amber-500/30">
        <div class="flex items-center justify-between mb-2">
          <h3 class="text-sm font-semibold text-amber-300">New Student Credentials — shown once, copy them now</h3>
          <button id="btn-dismiss-credentials" class="text-slate-400 hover:text-white text-lg leading-none">&times;</button>
        </div>
        <p class="text-xs text-slate-500 mb-3">These passwords aren't stored in plaintext and can't be shown again. Distribute them to students, then dismiss this panel.</p>
        <div class="max-h-64 overflow-y-auto rounded-lg border border-slate-800">
          <table class="w-full text-sm">
            <thead class="bg-slate-900/60 text-slate-400 text-xs uppercase tracking-wider">
              <tr><th class="text-left px-3 py-2">Name</th><th class="text-left px-3 py-2">Staff ID</th><th class="text-left px-3 py-2">Password</th></tr>
            </thead>
            <tbody>
              ${creds.map(c => `
                <tr class="border-t border-slate-800">
                  <td class="px-3 py-2 text-white">${this.esc(c.name)}</td>
                  <td class="px-3 py-2 font-mono text-cyan-300">${this.esc(c.staff_id)}</td>
                  <td class="px-3 py-2 font-mono text-emerald-300">${this.esc(c.password)}</td>
                </tr>
              `).join('')}
            </tbody>
          </table>
        </div>
        <button id="btn-copy-credentials" class="mt-3 text-xs px-3 py-1.5 rounded-lg border border-slate-700/50 text-slate-300 hover:border-emerald-500/50 hover:text-emerald-300 transition-colors">
          Copy as CSV
        </button>
      </div>
    `;
  }

  private renderStudentTable(): string {
    const students = this.dashboard!.students;
    return `
      <div class="glass-card p-6">
        <h2 class="text-lg font-semibold text-white mb-4">Roster &amp; Progress (${students.length})</h2>
        ${students.length === 0 ? `<p class="text-sm text-slate-500">No students imported yet.</p>` : `
          <div class="space-y-2">
            ${students.map(s => `
              <details class="rounded-lg border border-slate-800 bg-slate-900/40 group">
                <summary class="flex items-center justify-between px-4 py-3 cursor-pointer list-none">
                  <div>
                    <div class="text-sm font-medium text-white">${this.esc(s.name)}</div>
                    <div class="text-xs text-slate-500">${this.esc(s.external_id || s.staff_id)} · ${s.session_count} session${s.session_count === 1 ? '' : 's'}</div>
                  </div>
                  <div class="flex items-center gap-4">
                    <div class="text-right">
                      <div class="text-sm font-semibold ${this.scoreColor(s.avg_score)}">${s.session_count > 0 ? s.avg_score.toFixed(0) + '%' : '—'}</div>
                      <div class="text-[10px] text-slate-500 uppercase tracking-wider">avg score</div>
                    </div>
                    <button data-staff-id="${this.esc(s.staff_id)}" class="btn-remove-student text-slate-500 hover:text-red-400 text-xs">Remove</button>
                  </div>
                </summary>
                <div class="border-t border-slate-800 px-4 py-3">
                  ${s.sessions.length === 0 ? `<p class="text-xs text-slate-500">No completed sessions yet.</p>` : `
                    <table class="w-full text-xs">
                      <thead class="text-slate-500 uppercase tracking-wider">
                        <tr><th class="text-left py-1">Scenario</th><th class="text-left py-1">Outcome</th><th class="text-left py-1">Score</th><th class="text-left py-1">Duration</th><th class="text-left py-1">Missed</th><th class="text-left py-1"></th></tr>
                      </thead>
                      <tbody>
                        ${s.sessions.map(sess => `
                          <tr class="border-t border-slate-800/60">
                            <td class="py-1.5 text-slate-300">${this.esc(sess.scenario_id)}</td>
                            <td class="py-1.5 text-slate-300">${this.esc(sess.outcome)}</td>
                            <td class="py-1.5 ${this.scoreColor(sess.score)}">${sess.score.toFixed(0)}%</td>
                            <td class="py-1.5 text-slate-400">${Math.round(sess.duration_seconds / 60)}m</td>
                            <td class="py-1.5 text-slate-400">${sess.missed_critical_windows}</td>
                            <td class="py-1.5 text-right">
                              <button data-session-id="${this.esc(sess.session_id)}" class="btn-view-report ${sess.session_id === this.viewingReportSessionId ? 'text-cyan-200 font-semibold' : 'text-cyan-400 hover:text-cyan-300'}">${sess.session_id === this.viewingReportSessionId ? 'Viewing' : 'View Report'}</button>
                            </td>
                          </tr>
                        `).join('')}
                      </tbody>
                    </table>
                  `}
                </div>
              </details>
            `).join('')}
          </div>
        `}
      </div>
    `;
  }

  private renderMissedInterventionsPanel(): string {
    const items = this.dashboard!.top_missed_interventions;
    return `
      <div class="glass-card p-6">
        <h2 class="text-lg font-semibold text-white mb-1">Most-Missed Interventions</h2>
        <p class="text-xs text-slate-500 mb-4">Aggregated across every completed session in this cohort — where the class is actually struggling.</p>
        ${items.length === 0 ? `<p class="text-sm text-slate-500">No failure events recorded yet.</p>` : `
          <div class="space-y-2">
            ${items.slice(0, 10).map(m => `
              <div class="flex items-center justify-between rounded-lg border border-slate-800 bg-slate-900/40 px-4 py-2.5">
                <div>
                  <div class="text-sm text-white">${this.esc(m.failure)}</div>
                  <div class="text-xs text-slate-500">${this.esc(m.scenario_id)}</div>
                </div>
                <div class="text-sm font-semibold text-amber-400">${m.count}×</div>
              </div>
            `).join('')}
          </div>
        `}
      </div>
    `;
  }

  private scoreColor(score: number): string {
    if (score >= 80) return 'text-green-400';
    if (score >= 60) return 'text-amber-400';
    return 'text-red-400';
  }

  private esc(s: string): string {
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  mount(container: HTMLElement): void {
    container.innerHTML = this.render();
    this.element = container;
    this.setupEventListeners();
    this.loadCohorts();
  }

  unmount(): void {
    this.element = null;
  }

  private setupEventListeners(): void {
    this.on('#btn-signout', 'click', () => this.config.onSignOut());
    this.rebindCohortListListeners();
    this.attachDetailListeners();
  }

  private attachDetailListeners(): void {
    this.on('#import-roster-form', 'submit', async (e) => {
      e.preventDefault();
      const textarea = this.querySelector<HTMLTextAreaElement>('#roster-textarea');
      const raw = textarea?.value || '';
      const students = raw
        .split('\n')
        .map(line => line.trim())
        .filter(line => line.length > 0)
        .map(line => {
          const [name, ...rest] = line.split(',');
          return { name: name.trim(), external_id: rest.join(',').trim() };
        })
        .filter(s => s.name.length > 0);

      if (students.length === 0) {
        showToast('Enter at least one student (one per line)', 'error');
        return;
      }
      try {
        const credentials = await apiClient.importRoster(this.selectedCohortId!, students);
        this.lastCredentials = credentials;
        showToast(`Imported ${credentials.length} student${credentials.length === 1 ? '' : 's'}`, 'success');
        await this.loadDashboard();
        this.syncSelectedCohortStudentCount();
      } catch (err: any) {
        showToast(err?.message || 'Failed to import roster', 'error');
      }
    });
    this.on('#btn-dismiss-credentials', 'click', () => {
      this.lastCredentials = null;
      this.refreshCohortDetailPanel();
    });
    this.on('#btn-copy-credentials', 'click', async () => {
      if (!this.lastCredentials) return;
      const csv = ['name,staff_id,password', ...this.lastCredentials.map(c => `${c.name},${c.staff_id},${c.password}`)].join('\n');
      try {
        await navigator.clipboard.writeText(csv);
        showToast('Copied to clipboard', 'success');
      } catch {
        showToast('Copy failed — select and copy manually', 'error');
      }
    });
    this.on('.btn-remove-student', 'click', async (e) => {
      e.preventDefault();
      const staffId = (e.currentTarget as HTMLElement).dataset.staffId!;
      try {
        await apiClient.removeStudent(this.selectedCohortId!, staffId);
        showToast('Student removed from cohort', 'success');
        await this.loadDashboard();
        this.syncSelectedCohortStudentCount();
      } catch (err: any) {
        showToast(err?.message || 'Failed to remove student', 'error');
      }
    });
    this.on('.btn-view-report', 'click', async (e) => {
      e.preventDefault();
      const sessionId = (e.currentTarget as HTMLElement).dataset.sessionId!;
      this.viewingReportSessionId = sessionId;
      this.loadingReport = true;
      this.viewingReport = null;
      this.refreshCohortDetailPanel();
      try {
        this.viewingReport = await apiClient.getTrainingReport(sessionId);
      } catch (err: any) {
        showToast(err?.message || 'Failed to load report', 'error');
        this.viewingReportSessionId = null;
      }
      this.loadingReport = false;
      this.refreshCohortDetailPanel();
    });
    this.on('#btn-dismiss-report', 'click', () => {
      this.viewingReport = null;
      this.viewingReportSessionId = null;
      this.refreshCohortDetailPanel();
    });
  }

  private async loadCohorts(): Promise<void> {
    this.loadingCohorts = true;
    this.loadError = null;
    this.refreshCohortListPanel();
    try {
      this.cohorts = await apiClient.listCohorts();
      this.loadingCohorts = false;
    } catch (err: any) {
      this.loadingCohorts = false;
      const message = err?.message || 'Failed to load cohorts';
      this.loadError = message;
      showToast(message, 'error');
    }
    this.refreshCohortListPanel();
  }

  private async loadDashboard(): Promise<void> {
    if (!this.selectedCohortId) return;
    this.loadingDashboard = true;
    this.dashboard = null;
    this.refreshCohortDetailPanel();
    try {
      this.dashboard = await apiClient.getCohortDashboard(this.selectedCohortId);
    } catch (err: any) {
      showToast(err?.message || 'Failed to load cohort dashboard', 'error');
    }
    this.loadingDashboard = false;
    this.refreshCohortDetailPanel();
  }

  // The sidebar's student_count comes from a separate list-cohorts call
  // (Cohort has no live-updating link to CohortDashboard), so importing or
  // removing a student updates this.dashboard but leaves the sidebar count
  // stale until this runs -- reconcile from the just-reloaded dashboard's
  // actual student array rather than a round trip back to listCohorts().
  private syncSelectedCohortStudentCount(): void {
    if (!this.dashboard || !this.selectedCohortId) return;
    const cohort = this.cohorts.find(c => c.cohort_id === this.selectedCohortId);
    if (cohort) cohort.student_count = this.dashboard.students.length;
    this.refreshCohortListPanel();
  }

  private refreshCohortListPanel(): void {
    const el = this.element?.querySelector('#cohort-list-panel');
    if (el) {
      el.innerHTML = this.renderCohortListPanel();
      // Full innerHTML replacement drops previously-bound listeners on
      // elements inside this panel (new-cohort form, cohort-item buttons),
      // so they need re-attaching -- the detail panel's own listeners are
      // untouched since it's a sibling container.
      this.rebindCohortListListeners();
    }
  }

  private refreshCohortDetailPanel(): void {
    const el = this.element?.querySelector('#cohort-detail-panel');
    if (el) {
      el.innerHTML = this.renderCohortDetailPanel();
      this.attachDetailListeners();
    }
  }

  private rebindCohortListListeners(): void {
    this.on('#btn-new-cohort', 'click', () => {
      this.showNewCohortForm = !this.showNewCohortForm;
      this.refreshCohortListPanel();
    });
    this.on('#new-cohort-form', 'submit', async (e) => {
      e.preventDefault();
      const input = this.querySelector<HTMLInputElement>('#new-cohort-name');
      const name = input?.value.trim();
      if (!name) return;
      try {
        const cohort = await apiClient.createCohort(name);
        this.cohorts.push(cohort);
        this.showNewCohortForm = false;
        this.selectedCohortId = cohort.cohort_id;
        this.refreshCohortListPanel();
        await this.loadDashboard();
        showToast(`Cohort "${cohort.name}" created`, 'success');
      } catch (err: any) {
        showToast(err?.message || 'Failed to create cohort', 'error');
      }
    });
    this.on('.cohort-item', 'click', async (e) => {
      const id = (e.currentTarget as HTMLElement).dataset.cohortId!;
      this.selectedCohortId = id;
      this.lastCredentials = null;
      this.viewingReport = null;
      this.viewingReportSessionId = null;
      this.refreshCohortListPanel();
      await this.loadDashboard();
    });
  }
}
