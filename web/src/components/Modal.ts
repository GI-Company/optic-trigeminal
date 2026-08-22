import { Component } from './component';

export interface ModalField {
  id: string;
  label: string;
  type: 'text' | 'number' | 'select' | 'textarea';
  placeholder?: string;
  options?: { value: string; label: string }[];
  required?: boolean;
  defaultValue?: string;
}

export interface ModalConfig {
  title: string;
  fields: ModalField[];
  submitLabel: string;
  onSubmit: (values: Record<string, string>) => void | Promise<void>;
  onCancel: () => void;
}

// Small generic form modal -- Admit/Discharge/Assign/Override all need a
// short one-off form (a handful of fields, submit or cancel) and this app
// has no framework/router-level dialog primitive, so this exists once and
// gets reused by all four instead of four bespoke inline forms.
export class Modal extends Component {
  private config: ModalConfig;

  constructor(config: ModalConfig) {
    super();
    this.config = config;
  }

  render(): string {
    const { title, fields, submitLabel } = this.config;
    return `
      <div id="modal-backdrop" class="fixed inset-0 z-[100] bg-black/60 backdrop-blur-sm flex items-center justify-center p-4">
        <div class="glass-card w-full max-w-md p-6 relative" role="dialog" aria-modal="true">
          <div class="flex items-center justify-between mb-5">
            <h3 class="text-lg font-semibold text-white">${title}</h3>
            <button id="modal-close" class="text-slate-400 hover:text-white text-xl leading-none">&times;</button>
          </div>
          <form id="modal-form" class="space-y-4">
            ${fields.map(f => this.renderField(f)).join('')}
            <div id="modal-error" class="hidden text-sm text-red-400"></div>
            <div class="flex gap-3 pt-2">
              <button type="button" id="modal-cancel" class="flex-1 px-4 py-2 rounded-lg border border-white/10 text-slate-300 hover:bg-slate-900/50 transition-colors">
                Cancel
              </button>
              <button type="submit" id="modal-submit" class="flex-1 px-4 py-2 rounded-lg bg-gradient-to-r from-cyan-500 to-blue-600 text-white font-medium hover:opacity-90 transition-opacity">
                ${submitLabel}
              </button>
            </div>
          </form>
        </div>
      </div>
    `;
  }

  private renderField(f: ModalField): string {
    const req = f.required ? 'required' : '';
    const baseClasses = 'w-full px-3 py-2 rounded-lg bg-slate-900/50 border border-white/10 text-white placeholder-slate-500 focus:outline-none focus:border-cyan-500/50';
    let control = '';
    if (f.type === 'select') {
      control = `
        <select id="field-${f.id}" name="${f.id}" class="${baseClasses}" ${req}>
          ${(f.options || []).map(o => `<option value="${this.escapeAttr(o.value)}">${this.escapeAttr(o.label)}</option>`).join('')}
        </select>
      `;
    } else if (f.type === 'textarea') {
      control = `<textarea id="field-${f.id}" name="${f.id}" class="${baseClasses}" rows="3" placeholder="${this.escapeAttr(f.placeholder || '')}" ${req}>${this.escapeAttr(f.defaultValue || '')}</textarea>`;
    } else {
      control = `<input id="field-${f.id}" name="${f.id}" type="${f.type}" class="${baseClasses}" placeholder="${this.escapeAttr(f.placeholder || '')}" value="${this.escapeAttr(f.defaultValue || '')}" ${req} />`;
    }
    return `
      <div>
        <label for="field-${f.id}" class="block text-xs font-medium text-slate-400 mb-1.5">${f.label}</label>
        ${control}
      </div>
    `;
  }

  private escapeAttr(s: string): string {
    return s.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');
  }

  mount(container: HTMLElement): void {
    container.innerHTML = this.render();
    this.element = container;
    this.setupEventListeners();
  }

  unmount(): void {
    if (this.element) this.element.innerHTML = '';
    this.element = null;
  }

  showError(message: string): void {
    const el = this.querySelector<HTMLElement>('#modal-error');
    if (el) {
      el.textContent = message;
      el.classList.remove('hidden');
    }
  }

  private setupEventListeners(): void {
    this.on('#modal-close', 'click', () => this.config.onCancel());
    this.on('#modal-cancel', 'click', () => this.config.onCancel());
    this.on('#modal-backdrop', 'click', (e) => {
      if (e.target === e.currentTarget) this.config.onCancel();
    });
    this.on('#modal-form', 'submit', async (e) => {
      e.preventDefault();
      const values: Record<string, string> = {};
      for (const f of this.config.fields) {
        const el = this.querySelector<HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement>(`#field-${f.id}`);
        values[f.id] = el?.value.trim() || '';
      }
      const submitBtn = this.querySelector<HTMLButtonElement>('#modal-submit');
      if (submitBtn) submitBtn.disabled = true;
      try {
        await this.config.onSubmit(values);
      } finally {
        if (submitBtn) submitBtn.disabled = false;
      }
    });
  }
}
