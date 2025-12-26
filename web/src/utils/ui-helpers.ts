export function showToast(message: string, type: 'success' | 'error' | 'info' = 'info'): void {
  const container = document.getElementById('system-log');
  if (!container) return;

  const entry = document.createElement('div');
  const colors = {
    success: 'text-green-400',
    error: 'text-red-400',
    info: 'text-slate-400'
  };

  entry.className = `${colors[type]} border-t border-slate-700 pt-1 px-2 py-1`;
  entry.textContent = `[${type.toUpperCase()}] ${message}`;
  container.appendChild(entry);
  container.scrollTop = container.scrollHeight;

  if (type !== 'error') {
    setTimeout(() => entry.remove(), 5000);
  }
}

export function debounce<T extends (...args: any[]) => any>(
  fn: T,
  delay: number
): (...args: Parameters<T>) => void {
  let timeoutId: NodeJS.Timeout;
  return (...args: Parameters<T>) => {
    clearTimeout(timeoutId);
    timeoutId = setTimeout(() => fn(...args), delay);
  };
}

export function formatDuration(ms: number): string {
  const seconds = Math.floor(ms / 1000);
  const minutes = Math.floor(seconds / 60);
  const remainingSeconds = seconds % 60;

  if (minutes > 0) {
    return `${minutes}m ${remainingSeconds}s`;
  }
  return `${remainingSeconds}s`;
}

export function formatDate(date: Date): string {
  return date.toLocaleString();
}

export function getVitalColor(vital: string, value: number): string {
  const ranges: Record<string, { normal: [number, number]; warning: [number, number] }> = {
    hr: { normal: [60, 100], warning: [50, 120] },
    rr: { normal: [12, 20], warning: [10, 30] },
    spo2: { normal: [95, 100], warning: [90, 100] },
    bp_sys: { normal: [90, 120], warning: [80, 140] },
    temp: { normal: [36.5, 37.5], warning: [35, 39] }
  };

  if (!ranges[vital]) return 'text-slate-400';

  const { normal, warning } = ranges[vital];

  if (value >= normal[0] && value <= normal[1]) {
    return 'text-green-400';
  }
  if (value >= warning[0] && value <= warning[1]) {
    return 'text-yellow-400';
  }
  return 'text-red-400';
}

export function createLoadingSpinner(): HTMLElement {
  const div = document.createElement('div');
  div.className = 'flex items-center justify-center h-64';
  div.innerHTML = `
    <div class="text-center">
      <div class="animate-spin rounded-full h-12 w-12 border-b-2 border-cyan-400 mx-auto mb-4"></div>
      <p class="text-slate-400">Loading...</p>
    </div>
  `;
  return div;
}
