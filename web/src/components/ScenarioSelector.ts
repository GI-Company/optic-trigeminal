import { Component } from './component';
import type { TrainingScenario } from '@api/types';

export interface ScenarioSelectorConfig {
  scenarios: TrainingScenario[];
  onSelect: (scenarioId: string) => void;
  onCancel: () => void;
}

export class ScenarioSelector extends Component {
  private config: ScenarioSelectorConfig;

  constructor(config: ScenarioSelectorConfig) {
    super();
    this.config = config;
  }

  render(): string {
    return `
      <div class="fixed inset-0 bg-black/50 flex items-center justify-center z-50">
        <div class="bg-slate-800 border border-cyan-500/30 rounded-lg p-6 max-w-md w-full mx-4">
          <h2 class="text-2xl font-bold text-cyan-400 mb-6">Select Training Scenario</h2>
          <div class="space-y-3 max-h-96 overflow-y-auto">
            ${this.config.scenarios.map(scenario => `
              <button
                data-scenario="${scenario.id}"
                class="scenario-btn w-full p-4 text-left bg-slate-700 hover:bg-slate-600 border border-slate-600 hover:border-cyan-500 rounded transition"
              >
                <div class="font-semibold text-cyan-300">${scenario.title}</div>
                <div class="text-xs text-slate-400">${scenario.category} • ${scenario.difficulty} • ${scenario.duration_min} min</div>
              </button>
            `).join('')}
          </div>
          <button
            id="btn-close-scenarios"
            class="mt-6 w-full py-2 bg-slate-600 hover:bg-slate-500 rounded text-slate-200 font-semibold"
          >
            Cancel
          </button>
        </div>
      </div>
    `;
  }

  mount(container: HTMLElement): void {
    const modal = document.createElement('div');
    modal.innerHTML = this.render();
    const element = modal.firstElementChild as HTMLElement;
    container.appendChild(element);
    this.element = element;
    this.setupEventListeners();
  }

  unmount(): void {
    this.element?.remove();
    this.element = null;
  }

  private setupEventListeners(): void {
    this.on('.scenario-btn', 'click', (e) => {
      const button = (e.target as HTMLElement).closest('[data-scenario]') as HTMLElement;
      if (button) {
        const scenarioId = button.getAttribute('data-scenario')!;
        this.config.onSelect(scenarioId);
      }
    });

    this.on('#btn-close-scenarios', 'click', () => {
      this.config.onCancel();
    });
  }
}
