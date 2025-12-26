import { Component } from './component';

export class ConnectionError extends Component {
  render(): string {
    return `
      <div class="min-h-screen bg-slate-900 flex items-center justify-center">
        <div class="text-center">
          <div class="text-4xl font-bold text-red-400 mb-4">Connection Error</div>
          <p class="text-slate-400 mb-4">Could not connect to backend server at http://localhost:8080</p>
          <p class="text-slate-500 text-sm">Make sure the server is running:</p>
          <code class="block bg-slate-800 p-4 rounded mt-4 text-cyan-400">./build/optic-trigeminal</code>
          <button
            id="btn-retry"
            class="mt-8 px-6 py-2 bg-cyan-600 hover:bg-cyan-500 text-white rounded font-semibold"
          >
            Retry Connection
          </button>
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
    this.on('#btn-retry', 'click', () => {
      window.location.reload();
    });
  }
}
