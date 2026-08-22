import type { Role } from '../api/types';

export interface SignInResult {
  token: string;
  userId: string;
  staffName: string;
  role: Role;
}

// Matches start_demo.sh, which sets every seeded demo account
// (RN_001/CHARGE_001/PROVIDER_001/ADMIN_001/IT_001/INSTRUCTOR_001) to this
// same password via ACMK_*_PASSWORD env vars. Real auth still runs -- this
// only saves typing it in; it does nothing if the server was started with
// its own random-generated passwords instead (see the README's "Demo
// credentials" section for that case).
const DEMO_PASSWORD = 'demo1234';

interface SignInProps {
  onSignIn: (staffId: string, password: string) => Promise<SignInResult>;
  onSignedIn: (result: SignInResult) => void;
}

// Real credential form -- authenticates against POST /api/auth/sign-in.
// The role shown on the dashboard afterward comes from the server's
// response, never from anything the user picks here. See
// docs/API_REFERENCE.md's "Credential setup" section for how the demo
// accounts (RN_001, CHARGE_001, PROVIDER_001, ADMIN_001, IT_001,
// INSTRUCTOR_001) get their
// passwords on first server boot.
export class SignInScreen {
  private props: SignInProps;
  private container: HTMLElement | null = null;
  private submitting = false;

  constructor(props: SignInProps) {
    this.props = props;
  }

  mount(parent: HTMLElement): void {
    this.container = document.createElement('div');
    this.container.className = 'min-h-screen flex items-center justify-center relative overflow-hidden bg-slate-950 p-4';

    this.container.innerHTML = `
      <div class="absolute top-[-10%] left-[-10%] w-[40%] h-[40%] rounded-full bg-cyan-600/20 blur-[120px]"></div>
      <div class="absolute bottom-[-10%] right-[-10%] w-[40%] h-[40%] rounded-full bg-blue-600/20 blur-[120px]"></div>

      <div class="glass-panel w-full max-w-sm rounded-2xl p-8 relative z-10 animate-fade-in-up">
        <div class="text-center mb-8">
          <div class="inline-flex items-center justify-center w-16 h-16 rounded-2xl bg-gradient-to-br from-cyan-500 to-blue-600 mb-4 shadow-lg shadow-cyan-500/30">
            <svg class="w-8 h-8 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19.428 15.428a2 2 0 00-1.022-.547l-2.387-.477a6 6 0 00-3.86.517l-.318.158a6 6 0 01-3.86.517L6.05 15.21a2 2 0 00-1.806.547M8 4h8l-1 1v5.172a2 2 0 00.586 1.414l5 5c1.26 1.26.367 3.414-1.415 3.414H4.828c-1.782 0-2.674-2.154-1.414-3.414l5-5A2 2 0 009 10.172V5L8 4z"></path>
            </svg>
          </div>
          <h1 class="text-3xl font-bold text-white mb-2 tracking-tight">OpticTrigeminal</h1>
          <p class="text-slate-400">Clinical Edge Intelligence</p>
        </div>

        <form id="sign-in-form" class="space-y-4" autocomplete="off">
          <div>
            <label for="staff-id" class="block text-xs font-medium uppercase tracking-wider text-slate-500 mb-1.5">Staff ID</label>
            <input id="staff-id" name="staff-id" type="text" placeholder="RN_001" autocomplete="username"
              class="w-full rounded-xl border border-slate-700/50 bg-slate-800/30 px-4 py-3 text-white placeholder-slate-600 outline-none transition-colors focus:border-cyan-500/60 focus:bg-slate-800/60" />
          </div>
          <div>
            <label for="password" class="block text-xs font-medium uppercase tracking-wider text-slate-500 mb-1.5">Password</label>
            <input id="password" name="password" type="password" placeholder="••••••••" autocomplete="current-password"
              class="w-full rounded-xl border border-slate-700/50 bg-slate-800/30 px-4 py-3 text-white placeholder-slate-600 outline-none transition-colors focus:border-cyan-500/60 focus:bg-slate-800/60" />
          </div>

          <div id="sign-in-error" class="hidden rounded-lg border border-red-500/30 bg-red-500/10 px-3 py-2 text-sm text-red-400"></div>

          <button id="sign-in-btn" type="submit" class="btn-neon w-full py-4 flex items-center justify-center gap-2">
            <span id="sign-in-btn-label">Authenticate</span>
            <svg id="sign-in-btn-icon" class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M11 16l-4-4m0 0l4-4m-4 4h14m-5 4v1a3 3 0 01-3 3H6a3 3 0 01-3-3V7a3 3 0 013-3h7a3 3 0 013 3v1"></path>
            </svg>
          </button>
        </form>

        <div class="mt-6 pt-6 border-t border-slate-800">
          <p class="text-[10px] font-medium uppercase tracking-wider text-slate-500 mb-3 text-center">Quick Demo Sign-In</p>
          <div class="grid grid-cols-2 gap-2">
            <button type="button" class="demo-login-btn text-xs px-3 py-2 rounded-lg border border-slate-700/50 bg-slate-800/30 text-slate-300 hover:border-cyan-500/50 hover:text-cyan-300 transition-colors" data-staff-id="RN_001">Nurse</button>
            <button type="button" class="demo-login-btn text-xs px-3 py-2 rounded-lg border border-slate-700/50 bg-slate-800/30 text-slate-300 hover:border-cyan-500/50 hover:text-cyan-300 transition-colors" data-staff-id="CHARGE_001">Charge Nurse</button>
            <button type="button" class="demo-login-btn text-xs px-3 py-2 rounded-lg border border-slate-700/50 bg-slate-800/30 text-slate-300 hover:border-cyan-500/50 hover:text-cyan-300 transition-colors" data-staff-id="PROVIDER_001">Provider</button>
            <button type="button" class="demo-login-btn text-xs px-3 py-2 rounded-lg border border-slate-700/50 bg-slate-800/30 text-slate-300 hover:border-cyan-500/50 hover:text-cyan-300 transition-colors" data-staff-id="ADMIN_001">Admin</button>
            <button type="button" class="demo-login-btn col-span-2 text-xs px-3 py-2 rounded-lg border border-emerald-700/50 bg-emerald-900/20 text-emerald-300 hover:border-emerald-500/50 hover:text-emerald-200 transition-colors" data-staff-id="INSTRUCTOR_001">Instructor (class cohorts)</button>
          </div>
        </div>

        <div class="mt-6 text-center">
          <p class="text-xs text-slate-600">HIPAA Compliant System • Edge-Native V3.0</p>
        </div>
      </div>
    `;

    // Clear the parent (e.g. index.html's static "Initializing clinical
    // interface..." loading skeleton, or a previously mounted screen)
    // before appending -- otherwise this screen just stacks on top of
    // whatever was already in there and the loading skeleton stays visible
    // forever even after initialization has actually finished.
    parent.innerHTML = '';
    parent.appendChild(this.container);
    this.attachEventListeners();
  }

  private attachEventListeners(): void {
    if (!this.container) return;

    const form = this.container.querySelector('#sign-in-form') as HTMLFormElement;
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      if (this.submitting) return;

      const staffId = (this.container!.querySelector('#staff-id') as HTMLInputElement).value.trim();
      const password = (this.container!.querySelector('#password') as HTMLInputElement).value;

      if (!staffId || !password) {
        this.showError('Enter a staff ID and password.');
        return;
      }

      await this.doSignIn(staffId, password);
    });

    this.container.querySelectorAll('.demo-login-btn').forEach(btn => {
      btn.addEventListener('click', async () => {
        if (this.submitting) return;
        const staffId = (btn as HTMLElement).dataset.staffId!;
        (this.container!.querySelector('#staff-id') as HTMLInputElement).value = staffId;
        (this.container!.querySelector('#password') as HTMLInputElement).value = DEMO_PASSWORD;
        await this.doSignIn(staffId, DEMO_PASSWORD);
      });
    });
  }

  private async doSignIn(staffId: string, password: string): Promise<void> {
    this.setSubmitting(true);
    try {
      const result = await this.props.onSignIn(staffId, password);
      this.props.onSignedIn(result);
    } catch (err: any) {
      // The most likely failure for the demo buttons specifically is the
      // server having been started without start_demo.sh (so the real
      // password is a random one printed to its console log, not
      // DEMO_PASSWORD) -- say that outright instead of a bare 401.
      this.showError(err?.message || 'Sign-in failed. If you used a Quick Demo Sign-In button, start the server with start_demo.sh so its password matches.');
      this.setSubmitting(false);
    }
  }

  private setSubmitting(submitting: boolean): void {
    this.submitting = submitting;
    const btn = this.container!.querySelector('#sign-in-btn') as HTMLButtonElement;
    const label = this.container!.querySelector('#sign-in-btn-label') as HTMLElement;
    const icon = this.container!.querySelector('#sign-in-btn-icon') as HTMLElement;

    btn.disabled = submitting;
    if (submitting) {
      label.textContent = 'Authenticating...';
      icon.outerHTML = '<div id="sign-in-btn-icon" class="w-5 h-5 border-2 border-white/30 border-t-white rounded-full animate-spin"></div>';
      this.hideError();
    } else {
      label.textContent = 'Authenticate';
    }
  }

  private showError(message: string): void {
    const el = this.container!.querySelector('#sign-in-error') as HTMLElement;
    el.textContent = message;
    el.classList.remove('hidden');
  }

  private hideError(): void {
    const el = this.container!.querySelector('#sign-in-error') as HTMLElement;
    el.classList.add('hidden');
  }

  unmount(): void {
    if (this.container && this.container.parentNode) {
      this.container.parentNode.removeChild(this.container);
      this.container = null;
    }
  }
}
