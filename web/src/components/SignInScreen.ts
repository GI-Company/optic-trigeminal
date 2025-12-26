import { Component } from './component';
import type { Role } from '@api/types';

export interface SignInScreenConfig {
  roles: Role[];
  roleDisplayNames: Record<Role, string>;
  onSignIn: (role: Role) => void;
}

export class SignInScreen extends Component {
  private config: SignInScreenConfig;
  private selectedRole: Role | null = null;

  constructor(config: SignInScreenConfig) {
    super();
    this.config = config;
  }

  render(): string {
    return `
      <div class="min-h-screen bg-gradient-to-b from-slate-900 to-slate-800 flex items-center justify-center p-4">
        <div class="max-w-md w-full">
          <div class="bg-slate-800 rounded-lg border border-cyan-500/30 p-8 shadow-2xl">
            <h1 class="text-4xl font-bold text-cyan-400 mb-2 text-center">OpticTrigeminal</h1>
            <p class="text-slate-400 text-center mb-8">Healthcare AI Clinical Dashboard</p>
            
            <p class="text-slate-300 text-sm mb-6 text-center">Select your role to begin</p>
            
            <div class="grid grid-cols-1 gap-3 mb-8">
              ${this.config.roles.map(role => `
                <button
                  data-role="${role}"
                  class="role-button py-3 px-4 rounded border border-cyan-500/50 bg-slate-700 hover:bg-cyan-500/20 hover:border-cyan-400 text-slate-200 font-semibold transition-all text-sm"
                >
                  ${this.config.roleDisplayNames[role]}
                </button>
              `).join('')}
            </div>
            
            <div id="signin-actions" class="hidden">
              <button
                id="btn-confirm-signin"
                class="w-full bg-cyan-600 hover:bg-cyan-500 text-white font-bold py-2 px-4 rounded mb-3 transition"
              >
                Sign In
              </button>
              <button
                id="btn-cancel-signin"
                class="w-full bg-slate-600 hover:bg-slate-500 text-white font-bold py-2 px-4 rounded transition"
              >
                Cancel
              </button>
            </div>
            
            <div class="text-xs text-slate-500 text-center mt-6 border-t border-slate-700 pt-4">
              For training and testing only
            </div>
          </div>
        </div>
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

  private setupEventListeners(): void {
    this.on('[data-role]', 'click', (e) => {
      const button = e.target as HTMLElement;
      const role = button.getAttribute('data-role') as Role;
      this.selectRole(role);
    });

    this.on('#btn-confirm-signin', 'click', () => {
      if (this.selectedRole) {
        this.config.onSignIn(this.selectedRole);
      }
    });

    this.on('#btn-cancel-signin', 'click', () => {
      this.cancelSelection();
    });
  }

  private selectRole(role: Role): void {
    this.selectedRole = role;
    
    this.querySelectorAll<HTMLElement>('[data-role]').forEach(btn => {
      btn.classList.remove('border-cyan-400', 'bg-cyan-500/20');
    });
    
    const selected = this.querySelector<HTMLElement>(`[data-role="${role}"]`);
    if (selected) {
      selected.classList.add('border-cyan-400', 'bg-cyan-500/20');
    }
    
    const actions = this.querySelector<HTMLElement>('#signin-actions');
    if (actions) {
      actions.classList.remove('hidden');
    }
  }

  private cancelSelection(): void {
    this.selectedRole = null;
    this.querySelectorAll<HTMLElement>('[data-role]').forEach(btn => {
      btn.classList.remove('border-cyan-400', 'bg-cyan-500/20');
    });
    
    const actions = this.querySelector<HTMLElement>('#signin-actions');
    if (actions) {
      actions.classList.add('hidden');
    }
  }
}
