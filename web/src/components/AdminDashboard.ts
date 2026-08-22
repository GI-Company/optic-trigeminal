import { Component } from './component';
import type { Patient } from '@api/types';
import { apiClient } from '@api/client';
import { showToast } from '@utils/ui-helpers';

export interface AdminDashboardConfig {
  currentStaffName: string;
  patients: Patient[];
  acmkSessionId: string | null;
  onSignOut: () => void;
}

interface AuditEntry {
  record_type: 'human_event' | 'provenance';
  event_type?: string;
  user_id?: string;
  scope?: string;
  content?: string;
  agent_who?: string;
  agent_role?: string;
  entity_what?: string;
  timestamp: number;
  prev_hash: string;
  hash: string;
}

// ADMIN/Audit role: oversight, not intervention. No charting, no patient
// selection into the clinical workflow -- just visibility into the patient
// roster plus the real, hash-chained audit trail
// (GET /api/acmk/audit/recent, ADMIN-only server-side; backed by
// audit_log.ndjson written by ACMK::DefaultEnvironmentIO).
export class AdminDashboard extends Component {
  private config: AdminDashboardConfig;
  private entries: AuditEntry[] = [];
  private loading = true;
  private loadError: string | null = null;
  private showCreateStaffForm = false;
  private lastCreatedCredential: { staff_id: string; role: string; password: string } | null = null;

  constructor(config: AdminDashboardConfig) {
    super();
    this.config = config;
  }

  render(): string {
    return `
      <div class="min-h-screen bg-slate-950 text-slate-100 p-6 relative overflow-hidden">
        <div class="fixed top-[-10%] left-[-10%] w-[40%] h-[40%] rounded-full bg-violet-600/10 blur-[120px] pointer-events-none"></div>
        <div class="fixed bottom-[-10%] right-[-10%] w-[40%] h-[40%] rounded-full bg-cyan-600/10 blur-[120px] pointer-events-none"></div>

        <div class="max-w-7xl mx-auto relative z-10 animate-fade-in-up space-y-6">
          ${this.renderHeader()}
          ${this.renderRosterSummary()}
          <div id="create-staff-panel">
            ${this.renderCreateStaffPanel()}
          </div>
          <div class="grid grid-cols-1 lg:grid-cols-2 gap-8">
            ${this.renderAuditPanel()}
            ${this.renderRoster()}
          </div>
        </div>
      </div>
    `;
  }

  private renderCreateStaffPanel(): string {
    return `
      <div class="glass-card p-6">
        <div class="flex items-center justify-between mb-1">
          <h2 class="text-lg font-semibold text-white">Create Staff Account</h2>
          <button id="btn-toggle-create-staff" class="text-xs text-violet-300 hover:text-violet-200">${this.showCreateStaffForm ? 'Cancel' : '+ New Account'}</button>
        </div>
        <p class="text-xs text-slate-500 mb-4">One-off account provisioning -- e.g. the first Instructor account for a school deployment. For bulk student rosters, an Instructor imports those themselves from their own dashboard.</p>
        ${this.showCreateStaffForm ? `
          <form id="create-staff-form" class="grid grid-cols-1 sm:grid-cols-4 gap-3 items-end">
            <div class="sm:col-span-1">
              <label class="block text-xs text-slate-500 mb-1">Staff ID</label>
              <input id="staff-create-id" type="text" required placeholder="INSTRUCTOR_002"
                class="w-full px-3 py-2 rounded-lg bg-slate-900/50 border border-white/10 text-white placeholder-slate-500 text-sm focus:outline-none focus:border-violet-500/50" />
            </div>
            <div class="sm:col-span-1">
              <label class="block text-xs text-slate-500 mb-1">Name</label>
              <input id="staff-create-name" type="text" required placeholder="Dr. Jane Lee"
                class="w-full px-3 py-2 rounded-lg bg-slate-900/50 border border-white/10 text-white placeholder-slate-500 text-sm focus:outline-none focus:border-violet-500/50" />
            </div>
            <div class="sm:col-span-1">
              <label class="block text-xs text-slate-500 mb-1">Role</label>
              <select id="staff-create-role" class="w-full px-3 py-2 rounded-lg bg-slate-900/50 border border-white/10 text-white text-sm focus:outline-none focus:border-violet-500/50">
                <option value="instructor">Instructor</option>
                <option value="rn">Nurse (RN/LPN)</option>
                <option value="charge_nurse">Charge Nurse</option>
                <option value="provider">Provider (MD/DO)</option>
                <option value="admin">Admin/Audit</option>
                <option value="it">IT/System</option>
              </select>
            </div>
            <div class="sm:col-span-1">
              <button type="submit" class="w-full px-3 py-2 rounded-lg bg-gradient-to-r from-violet-500 to-indigo-600 text-white text-sm font-medium hover:opacity-90 transition-opacity">
                Create
              </button>
            </div>
          </form>
        ` : ''}
        ${this.lastCreatedCredential ? `
          <div class="mt-4 rounded-lg border border-amber-500/30 bg-amber-500/5 p-4">
            <div class="flex items-center justify-between mb-1">
              <p class="text-xs font-semibold text-amber-300">Password shown once -- copy it now</p>
              <button id="btn-dismiss-created-credential" class="text-slate-400 hover:text-white text-lg leading-none">&times;</button>
            </div>
            <p class="text-sm font-mono text-white">${this.lastCreatedCredential.staff_id} (${this.lastCreatedCredential.role}) &mdash; <span class="text-emerald-300">${this.lastCreatedCredential.password}</span></p>
          </div>
        ` : ''}
      </div>
    `;
  }

  private renderHeader(): string {
    return `
      <header class="glass-panel rounded-2xl p-6 flex justify-between items-center">
        <div class="flex items-center gap-4">
          <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-violet-500 to-indigo-600 flex items-center justify-center shadow-lg shadow-violet-500/20">
            <svg class="w-6 h-6 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m5.618-4.016A11.955 11.955 0 0112 2.944a11.955 11.955 0 01-8.618 3.04A12.02 12.02 0 003 9c0 5.591 3.824 10.29 9 11.622 5.176-1.332 9-6.03 9-11.622 0-1.042-.133-2.052-.382-3.016z"></path>
            </svg>
          </div>
          <div>
            <h1 class="text-2xl font-bold text-white tracking-tight">OpticTrigeminal <span class="text-xs align-top bg-violet-500/20 text-violet-300 px-2 py-0.5 rounded-full ml-1">Admin/Audit</span></h1>
            <p class="text-slate-400 text-sm">Oversight &amp; compliance — read-only, no clinical intervention</p>
          </div>
        </div>
        <div class="flex items-center gap-6">
          <div class="text-right">
            <div class="text-sm font-semibold text-white">${this.config.currentStaffName}</div>
            <div class="text-xs text-violet-300 font-medium uppercase tracking-wider">Admin</div>
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

  private renderRosterSummary(): string {
    const total = this.config.patients.length;
    const critical = this.config.patients.filter(p => p.acuity_score >= 7).length;
    const stat = (label: string, value: number, color: string) => `
      <div class="glass-card p-4 flex flex-col items-center justify-center">
        <div class="text-2xl font-bold ${color}">${value}</div>
        <div class="text-xs text-slate-400 uppercase tracking-wider mt-1">${label}</div>
      </div>
    `;
    return `
      <div class="grid grid-cols-3 gap-4">
        ${stat('Patients Monitored', total, 'text-white')}
        ${stat('Critical Acuity', critical, 'text-red-400')}
        ${stat('Session', this.config.acmkSessionId ? 1 : 0, 'text-violet-300')}
      </div>
    `;
  }

  private renderAuditPanel(): string {
    return `
      <div class="glass-card p-6">
        <div class="flex items-center justify-between mb-1">
          <h2 class="text-lg font-semibold text-white flex items-center gap-2">
            <svg class="w-5 h-5 text-violet-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 5H7a2 2 0 00-2 2v12a2 2 0 002 2h10a2 2 0 002-2V7a2 2 0 00-2-2h-2M9 5a2 2 0 002 2h2a2 2 0 002-2M9 5a2 2 0 012-2h2a2 2 0 012 2"></path>
            </svg>
            Audit Trail
          </h2>
          <button id="btn-refresh-audit" class="text-xs text-violet-300 hover:text-violet-200">Refresh</button>
        </div>
        <p class="text-xs text-slate-500 mb-4">Hash-chained, append-only (audit_log.ndjson). Each entry's <code>hash</code> covers the previous entry's hash + its own content — editing history breaks the chain.</p>
        <div id="audit-list" class="space-y-2 max-h-[520px] overflow-y-auto pr-1">
          ${this.renderAuditList()}
        </div>
      </div>
    `;
  }

  private renderAuditList(): string {
    if (this.loading) {
      return `<div class="text-sm text-slate-500 py-8 text-center">Loading audit trail…</div>`;
    }
    if (this.loadError) {
      return `<div class="text-sm text-red-400 py-8 text-center">${this.loadError}</div>`;
    }
    if (this.entries.length === 0) {
      return `<div class="text-sm text-slate-500 py-8 text-center">No audit entries yet. Actions taken in Clinical dashboards will appear here.</div>`;
    }

    return this.entries.slice().reverse().map(entry => {
      const time = new Date(entry.timestamp * 1000).toLocaleTimeString();
      const who = entry.user_id || entry.agent_who || 'unknown';
      const what = entry.event_type || entry.entity_what || entry.record_type;
      const detail = entry.content || entry.agent_role || '';
      return `
        <div class="rounded-lg border border-slate-800 bg-slate-900/40 px-4 py-3">
          <div class="flex items-center justify-between text-xs text-slate-500 mb-1">
            <span>${time}</span>
            <span class="uppercase tracking-wider">${entry.record_type.replace('_', ' ')}</span>
          </div>
          <div class="text-sm text-slate-200"><span class="font-semibold text-violet-300">${who}</span> — ${what}</div>
          ${detail ? `<div class="text-xs text-slate-400 mt-1">${detail}</div>` : ''}
          <div class="text-[10px] text-slate-600 mt-1.5 font-mono truncate">hash ${entry.hash.substring(0, 16)}…</div>
        </div>
      `;
    }).join('');
  }

  private renderRoster(): string {
    return `
      <div class="glass-card p-6">
        <h2 class="text-lg font-semibold text-white mb-4 flex items-center gap-2">
          <svg class="w-5 h-5 text-cyan-400" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M17 20h5v-2a3 3 0 00-5.356-1.857M17 20H7m10 0v-2c0-.656-.126-1.283-.356-1.857M7 20H2v-2a3 3 0 015.356-1.857M7 20v-2c0-.656.126-1.283.356-1.857m0 0a5.002 5.002 0 019.288 0M15 7a3 3 0 11-6 0 3 3 0 016 0zm6 3a2 2 0 11-4 0 2 2 0 014 0zM7 10a2 2 0 11-4 0 2 2 0 014 0z"></path>
          </svg>
          Patient Roster (read-only)
        </h2>
        <div class="space-y-2">
          ${this.config.patients.map(p => `
            <div class="flex items-center justify-between rounded-lg border border-slate-800 bg-slate-900/40 px-4 py-3">
              <div>
                <div class="text-sm font-medium text-white">${p.name}</div>
                <div class="text-xs text-slate-500">Room ${p.room} · ${p.mrn}</div>
              </div>
              <div class="text-xs font-semibold ${p.acuity_score >= 7 ? 'text-red-400' : p.acuity_score >= 4 ? 'text-amber-400' : 'text-green-400'}">
                Acuity ${p.acuity_score}
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
    this.setupEventListeners();
    this.loadAudit();
  }

  unmount(): void {
    this.element = null;
  }

  private setupEventListeners(): void {
    this.on('#btn-signout', 'click', () => this.config.onSignOut());
    this.on('#btn-refresh-audit', 'click', () => this.loadAudit());
    this.attachCreateStaffListeners();
  }

  private attachCreateStaffListeners(): void {
    this.on('#btn-toggle-create-staff', 'click', () => {
      this.showCreateStaffForm = !this.showCreateStaffForm;
      this.refreshCreateStaffPanel();
    });
    this.on('#create-staff-form', 'submit', async (e) => {
      e.preventDefault();
      const staffId = this.querySelector<HTMLInputElement>('#staff-create-id')?.value.trim() || '';
      const name = this.querySelector<HTMLInputElement>('#staff-create-name')?.value.trim() || '';
      const role = this.querySelector<HTMLSelectElement>('#staff-create-role')?.value as any;
      if (!staffId || !name) return;
      try {
        const result = await apiClient.createStaffAccount(staffId, name, role);
        this.lastCreatedCredential = { staff_id: result.staff_id, role: result.role, password: result.password };
        this.showCreateStaffForm = false;
        this.refreshCreateStaffPanel();
        showToast(`Created ${staffId}`, 'success');
      } catch (err: any) {
        showToast(err?.message || 'Failed to create staff account', 'error');
      }
    });
    this.on('#btn-dismiss-created-credential', 'click', () => {
      this.lastCreatedCredential = null;
      this.refreshCreateStaffPanel();
    });
  }

  private refreshCreateStaffPanel(): void {
    const el = this.element?.querySelector('#create-staff-panel');
    if (el) {
      el.innerHTML = this.renderCreateStaffPanel();
      this.attachCreateStaffListeners();
    }
  }

  private async loadAudit(): Promise<void> {
    this.loading = true;
    this.loadError = null;
    this.refreshAuditList();
    try {
      const result = await apiClient.getRecentAudit(100);
      this.entries = result.entries as AuditEntry[];
      this.loading = false;
    } catch (err: any) {
      this.loading = false;
      const message = err?.message || 'Failed to load audit trail';
      this.loadError = message;
      showToast(message, 'error');
    }
    this.refreshAuditList();
  }

  private refreshAuditList(): void {
    const el = this.element?.querySelector('#audit-list');
    if (el) el.innerHTML = this.renderAuditList();
  }
}
