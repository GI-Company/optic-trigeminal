import { SimulationStatus } from '@api/types';

export class SimulationModeBannerComponent {
  private container: HTMLElement;
  private simulationStatus: SimulationStatus | null = null;

  constructor(containerId: string) {
    const element = document.getElementById(containerId);
    if (!element) throw new Error(`Container ${containerId} not found`);
    this.container = element;
  }

  setSimulationStatus(status: SimulationStatus): void {
    this.simulationStatus = status;
    this.render();
  }

  private render(): void {
    this.container.innerHTML = '';

    if (!this.simulationStatus || !this.simulationStatus.is_simulation) {
      this.container.style.display = 'none';
      return;
    }

    this.container.style.display = 'block';
    this.container.className = 'simulation-mode-banner';

    const banner = document.createElement('div');
    banner.className = 'banner-content';
    banner.innerHTML = `
      <div class="banner-primary">
        <span class="warning-icon">⚠️</span>
        <span class="banner-text">
          <strong>SIMULATION MODE — NO REAL-WORLD EFFECT</strong>
        </span>
        <span class="close-icon">×</span>
      </div>
      <div class="banner-details">
        <span class="detail-item">
          <strong>Scenario:</strong> ${this.simulationStatus.scenario_id || 'Custom'}
        </span>
        <span class="detail-item">
          <strong>Session ID:</strong> <code>${this.simulationStatus.session_id.substring(0, 8)}…</code>
        </span>
        ${this.simulationStatus.is_active ? `
          <span class="detail-item elapsed">
            <strong>Elapsed:</strong> ${this.formatElapsed(this.simulationStatus.elapsed_ms)}
          </span>
        ` : ''}
      </div>
      <div class="banner-notice">
        <p>
          This is a training simulation. Clinical decisions made here are NOT connected to real patient records.
          No alerts have been sent. No real-world hooks are active. All outputs are tagged as non-operative.
        </p>
      </div>
    `;

    this.container.appendChild(banner);

    const closeButton = banner.querySelector('.close-icon') as HTMLElement;
    closeButton?.addEventListener('click', () => {
      this.container.style.display = 'none';
    });
  }

  private formatElapsed(ms: number): string {
    const totalSeconds = Math.floor(ms / 1000);
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = totalSeconds % 60;
    return `${minutes}m ${seconds}s`;
  }

  reset(): void {
    this.simulationStatus = null;
    this.render();
  }
}
