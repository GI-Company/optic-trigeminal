/**
 * OpticTrigeminal - Main Application Entry Point
 * Initializes the clinical dashboard and manages lifecycle
 */

import { apiClient } from '@api/client';
import { store } from '@store/state';
import type { Patient, Role, RoleCapabilities, ChartEntry } from '@api/types';
import './styles/index.css';

// ============================================================================
// APPLICATION INITIALIZATION
// ============================================================================

const app = document.getElementById('app')!;

async function initializeApp() {
  try {
    // Check backend connection
    const isHealthy = await apiClient.testConnection();
    if (!isHealthy) {
      showConnectionError();
      return;
    }

    // Render initial UI
    renderSignInScreen();

    // Load training scenarios in background
    const scenarios = await apiClient.listTrainingScenarios();
    store.setTrainingScenarios(scenarios);

    // Set up auto-refresh of patient data
    setupAutoRefresh();

  } catch (error) {
    console.error('Initialization failed:', error);
    showError('Failed to initialize application');
  }
}

// ============================================================================
// ROLE DEFINITIONS
// ============================================================================

const roleCapabilities: Record<Role, RoleCapabilities> = {
  rn: {
    displayName: 'Nurse (RN/LPN)',
    canViewVitals: true,
    canChartActions: true,
    canAddNotes: true,
    canAcceptRecommendations: true,
    canViewOrders: true,
    canAcknowledgeOrders: true,
    canAdmitPatient: false,
    canDischargePatient: false,
    canAssignPatients: false,
    canViewAllPatients: false,
    canOverrideAlerts: false,
    canViewAnalytics: false,
    canAccessAdmin: false
  },
  charge_nurse: {
    displayName: 'Charge Nurse',
    canViewVitals: true,
    canChartActions: true,
    canAddNotes: true,
    canAcceptRecommendations: true,
    canViewOrders: true,
    canAcknowledgeOrders: true,
    canAdmitPatient: true,
    canDischargePatient: true,
    canAssignPatients: true,
    canViewAllPatients: true,
    canOverrideAlerts: true,
    canViewAnalytics: true,
    canAccessAdmin: false
  },
  provider: {
    displayName: 'Provider (MD/DO)',
    canViewVitals: true,
    canChartActions: false,
    canAddNotes: true,
    canAcceptRecommendations: false,
    canViewOrders: true,
    canAcknowledgeOrders: false,
    canAdmitPatient: true,
    canDischargePatient: true,
    canAssignPatients: false,
    canViewAllPatients: true,
    canOverrideAlerts: true,
    canViewAnalytics: false,
    canAccessAdmin: false
  },
  admin: {
    displayName: 'Admin/Audit',
    canViewVitals: true,
    canChartActions: false,
    canAddNotes: false,
    canAcceptRecommendations: false,
    canViewOrders: true,
    canAcknowledgeOrders: false,
    canAdmitPatient: false,
    canDischargePatient: false,
    canAssignPatients: false,
    canViewAllPatients: true,
    canOverrideAlerts: false,
    canViewAnalytics: true,
    canAccessAdmin: true
  },
  it: {
    displayName: 'IT/System',
    canViewVitals: false,
    canChartActions: false,
    canAddNotes: false,
    canAcceptRecommendations: false,
    canViewOrders: false,
    canAcknowledgeOrders: false,
    canAdmitPatient: false,
    canDischargePatient: false,
    canAssignPatients: false,
    canViewAllPatients: false,
    canOverrideAlerts: false,
    canViewAnalytics: true,
    canAccessAdmin: true
  }
};

// ============================================================================
// UI RENDERING
// ============================================================================

function renderSignInScreen() {
  const roles: Role[] = ['rn', 'charge_nurse', 'provider', 'admin', 'it'];
  
  app.innerHTML = `
    <div class="min-h-screen bg-gradient-to-b from-slate-900 to-slate-800 flex items-center justify-center p-4">
      <div class="max-w-md w-full">
        <div class="bg-slate-800 rounded-lg border border-cyan-500/30 p-8 shadow-2xl">
          <h1 class="text-4xl font-bold text-cyan-400 mb-2 text-center">OpticTrigeminal</h1>
          <p class="text-slate-400 text-center mb-8">Healthcare AI Clinical Dashboard</p>
          
          <p class="text-slate-300 text-sm mb-6 text-center">Select your role to begin</p>
          
          <div class="grid grid-cols-1 gap-3 mb-8">
            ${roles.map(role => `
              <button
                data-role="${role}"
                class="role-button py-3 px-4 rounded border border-cyan-500/50 bg-slate-700 hover:bg-cyan-500/20 hover:border-cyan-400 text-slate-200 font-semibold transition-all text-sm"
              >
                ${roleCapabilities[role].displayName}
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

  // Attach event listeners
  let selectedRole: Role | null = null;

  document.querySelectorAll('.role-button').forEach(btn => {
    btn.addEventListener('click', (e) => {
      const role = (e.target as HTMLElement).getAttribute('data-role') as Role;
      selectedRole = role;
      
      document.querySelectorAll('.role-button').forEach(b => b.classList.remove('border-cyan-400', 'bg-cyan-500/20'));
      btn.classList.add('border-cyan-400', 'bg-cyan-500/20');
      
      document.getElementById('signin-actions')!.classList.remove('hidden');
    });
  });

  document.getElementById('btn-confirm-signin')?.addEventListener('click', () => {
    if (selectedRole) {
      const caps = roleCapabilities[selectedRole];
      store.signIn(selectedRole, caps.displayName);
      renderDashboard();
    }
  });

  document.getElementById('btn-cancel-signin')?.addEventListener('click', () => {
    selectedRole = null;
    document.querySelectorAll('.role-button').forEach(b => b.classList.remove('border-cyan-400', 'bg-cyan-500/20'));
    document.getElementById('signin-actions')!.classList.add('hidden');
  });
}

function renderDashboard() {
  const state = store.getState();
  
  app.innerHTML = `
    <div class="min-h-screen bg-slate-900 text-slate-100">
      <!-- Header -->
      <div class="bg-gradient-to-r from-cyan-900 to-blue-900 border-b border-cyan-500/30 p-4">
        <div class="max-w-7xl mx-auto flex justify-between items-center">
          <div class="flex items-center gap-4">
            <h1 class="text-2xl font-bold text-cyan-300">OpticTrigeminal</h1>
            <span class="text-xs bg-cyan-500/20 border border-cyan-500/50 px-3 py-1 rounded text-cyan-300">v3.0.0</span>
          </div>
          <div class="flex items-center gap-6">
            <div class="text-right">
              <div class="text-sm font-semibold">${state.currentStaffName}</div>
              <div class="text-xs text-slate-400">${state.currentRole?.toUpperCase()}</div>
            </div>
            <button
              id="btn-signout"
              class="px-4 py-2 bg-red-900/40 hover:bg-red-900/60 border border-red-700/50 rounded text-red-300 text-sm font-semibold"
            >
              Sign Out
            </button>
          </div>
        </div>
      </div>

      <!-- Main Content -->
      <div class="max-w-7xl mx-auto p-4">
        <div id="patient-grid" class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4 mb-8">
          <!-- Patients will be rendered here -->
        </div>
        
        <div id="training-section" class="bg-slate-800 border border-amber-700/50 rounded-lg p-6 mt-8">
          <h2 class="text-xl font-bold text-amber-300 mb-4">Clinical Training</h2>
          <button
            id="btn-start-training"
            class="px-6 py-3 bg-amber-700/40 hover:bg-amber-700/60 border border-amber-600/50 rounded text-amber-300 font-semibold"
          >
            Start Training Scenario
          </button>
        </div>
      </div>

      <!-- System Log -->
      <div id="system-log" class="fixed bottom-4 right-4 max-w-sm max-h-64 bg-slate-950 border border-slate-700 rounded overflow-y-auto font-mono text-xs text-slate-400"></div>
    </div>
  `;

  // Attach event listeners
  document.getElementById('btn-signout')?.addEventListener('click', () => {
    store.signOut();
    renderSignInScreen();
  });

  document.getElementById('btn-start-training')?.addEventListener('click', () => {
    renderTrainingScenarios();
  });

  // Load and render patients
  loadAndRenderPatients();
}

function renderTrainingScenarios() {
  const scenarios = store.getState().trainingScenarios;
  
  if (scenarios.length === 0) {
    showError('No training scenarios available');
    return;
  }

  const modal = document.createElement('div');
  modal.className = 'fixed inset-0 bg-black/50 flex items-center justify-center z-50';
  modal.innerHTML = `
    <div class="bg-slate-800 border border-cyan-500/30 rounded-lg p-6 max-w-md w-full mx-4">
      <h2 class="text-2xl font-bold text-cyan-400 mb-6">Select Training Scenario</h2>
      <div class="space-y-3 max-h-96 overflow-y-auto">
        ${scenarios.map(scenario => `
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
  `;
  
  document.body.appendChild(modal);

  document.querySelectorAll('.scenario-btn').forEach(btn => {
    btn.addEventListener('click', async (e) => {
      const scenarioId = (e.target as HTMLElement).closest('button')?.getAttribute('data-scenario');
      if (scenarioId) {
        modal.remove();
        await startTrainingSession(scenarioId);
      }
    });
  });

  document.getElementById('btn-close-scenarios')?.addEventListener('click', () => {
    modal.remove();
  });
}

async function startTrainingSession(scenarioId: string) {
  try {
    const session = await apiClient.startTraining(scenarioId);
    store.startTraining(session);
    renderTrainingMode();
  } catch (error) {
    showError(`Failed to start training: ${error}`);
  }
}

function renderTrainingMode() {
  const state = store.getState();
  const session = state.currentTrainingSession!;

  app.innerHTML = `
    <div class="min-h-screen bg-slate-900 text-slate-100">
      <!-- Header -->
      <div class="bg-gradient-to-r from-amber-900 to-orange-900 border-b border-amber-500/30 p-4">
        <div class="max-w-7xl mx-auto flex justify-between items-center">
          <div>
            <h1 class="text-2xl font-bold text-amber-300">${session.scenario.title}</h1>
            <div class="text-sm text-amber-200">Difficulty: ${session.scenario.difficulty}</div>
          </div>
          <div class="text-right">
            <div class="text-3xl font-bold text-amber-300">${(session.score * 100).toFixed(0)}%</div>
            <div class="text-sm text-amber-200">Time: ${Math.round(session.elapsed_ms / 1000)}s</div>
          </div>
        </div>
      </div>

      <!-- Patient Info -->
      <div class="max-w-7xl mx-auto p-4">
        <div class="grid grid-cols-4 gap-4 mb-6">
          <div class="bg-slate-800 border border-slate-700 p-4 rounded">
            <div class="text-xs text-slate-400">Heart Rate</div>
            <div class="text-3xl font-bold text-red-400">${session.patient.vitals.hr}</div>
          </div>
          <div class="bg-slate-800 border border-slate-700 p-4 rounded">
            <div class="text-xs text-slate-400">Respiratory Rate</div>
            <div class="text-3xl font-bold text-orange-400">${session.patient.vitals.rr}</div>
          </div>
          <div class="bg-slate-800 border border-slate-700 p-4 rounded">
            <div class="text-xs text-slate-400">SpO2</div>
            <div class="text-3xl font-bold text-cyan-400">${session.patient.vitals.spo2}%</div>
          </div>
          <div class="bg-slate-800 border border-slate-700 p-4 rounded">
            <div class="text-xs text-slate-400">Temperature</div>
            <div class="text-3xl font-bold text-yellow-400">${session.patient.vitals.temp.toFixed(1)}°C</div>
          </div>
        </div>

        <!-- Actions -->
        <div class="grid grid-cols-2 gap-3 mb-6">
          <button id="btn-assess" class="training-action-btn py-2 px-4 bg-blue-900/40 hover:bg-blue-900/60 border border-blue-700/50 rounded text-blue-300 font-semibold">
            Assess Patient
          </button>
          <button id="btn-oxygen" class="training-action-btn py-2 px-4 bg-green-900/40 hover:bg-green-900/60 border border-green-700/50 rounded text-green-300 font-semibold">
            Administer Oxygen
          </button>
          <button id="btn-position" class="training-action-btn py-2 px-4 bg-purple-900/40 hover:bg-purple-900/60 border border-purple-700/50 rounded text-purple-300 font-semibold">
            Position Patient
          </button>
          <button id="btn-notify" class="training-action-btn py-2 px-4 bg-red-900/40 hover:bg-red-900/60 border border-red-700/50 rounded text-red-300 font-semibold">
            Notify Provider
          </button>
        </div>

        <!-- End Training -->
        <button
          id="btn-end-training"
          class="w-full py-3 bg-slate-600 hover:bg-slate-500 rounded text-slate-100 font-bold"
        >
          End Training Session
        </button>
      </div>
    </div>
  `;

  // Attach action buttons
  document.getElementById('btn-assess')?.addEventListener('click', () => executeTrainingAction('assess_patient'));
  document.getElementById('btn-oxygen')?.addEventListener('click', () => executeTrainingAction('administer_oxygen', { lpm: 2 }));
  document.getElementById('btn-position')?.addEventListener('click', () => executeTrainingAction('position_patient'));
  document.getElementById('btn-notify')?.addEventListener('click', () => executeTrainingAction('notify_provider'));

  document.getElementById('btn-end-training')?.addEventListener('click', async () => {
    try {
      await apiClient.endTraining(false);
      store.endTraining();
      renderDashboard();
    } catch (error) {
      showError('Failed to end training');
    }
  });
}

async function executeTrainingAction(action: string, params?: Record<string, any>) {
  try {
    const response = await apiClient.executeTrainingAction({ action, parameters: params });
    const session = store.getState().currentTrainingSession!;
    store.updateTrainingSession({
      ...session,
      score: response.cumulative_score,
      elapsed_ms: session.elapsed_ms + 1000
    });
    renderTrainingMode(); // Re-render to update UI
  } catch (error) {
    showError(`Action failed: ${error}`);
  }
}

async function loadAndRenderPatients() {
  try {
    // Mock patient data for now
    const mockPatients: Patient[] = [
      {
        id: 1,
        name: 'Alice Johnson',
        mrn: 'MRN001',
        room: '201',
        admission_diagnosis: 'Acute Respiratory Distress',
        acuity_score: 8,
        vitals: {
          hr: 98,
          rr: 20,
          spo2: 94,
          bp_sys: 128,
          bp_dia: 82,
          temp: 37.8,
          is_crisis: false,
          drift_variance: 0.2
        },
        hr_history: Array(20).fill(98).map((v) => v + Math.random() * 10 - 5),
        spo2_history: Array(20).fill(94).map((v) => v + Math.random() * 5 - 2),
        temp_history: Array(20).fill(37.8),
        nurse_notes: ''
      }
    ];

    store.setPatients(mockPatients);

    const grid = document.getElementById('patient-grid');
    if (grid) {
      grid.innerHTML = mockPatients.map(patient => `
        <div class="bg-slate-800 border border-slate-700 rounded-lg p-4 hover:border-cyan-500/50 transition cursor-pointer" data-patient-id="${patient.id}">
          <h3 class="font-bold text-lg text-cyan-300">${patient.name}</h3>
          <div class="text-xs text-slate-400 mb-4">${patient.room} | ${patient.mrn}</div>
          
          <div class="grid grid-cols-2 gap-2 text-sm">
            <div class="bg-slate-700 p-2 rounded">
              <div class="text-xs text-slate-500">HR</div>
              <div class="font-bold ${patient.vitals.hr > 100 ? 'text-red-400' : 'text-green-400'}">${patient.vitals.hr}</div>
            </div>
            <div class="bg-slate-700 p-2 rounded">
              <div class="text-xs text-slate-500">SpO2</div>
              <div class="font-bold ${patient.vitals.spo2 < 95 ? 'text-red-400' : 'text-green-400'}">${patient.vitals.spo2}%</div>
            </div>
            <div class="bg-slate-700 p-2 rounded">
              <div class="text-xs text-slate-500">RR</div>
              <div class="font-bold text-blue-400">${patient.vitals.rr}</div>
            </div>
            <div class="bg-slate-700 p-2 rounded">
              <div class="text-xs text-slate-500">BP</div>
              <div class="font-bold text-yellow-400">${patient.vitals.bp_sys}/${patient.vitals.bp_dia}</div>
            </div>
          </div>
        </div>
      `).join('');

      document.querySelectorAll('[data-patient-id]').forEach(card => {
        card.addEventListener('click', () => {
          const patientId = parseInt((card as HTMLElement).getAttribute('data-patient-id')!);
          store.selectPatient(patientId);
          renderPatientDetail();
        });
      });
    }
  } catch (error) {
    showError(`Failed to load patients: ${error}`);
  }
}

function renderPatientDetail() {
  const patient = store.getSelectedPatient();
  if (!patient) return;

  const state = store.getState();
  const caps = roleCapabilities[state.currentRole!];

  app.innerHTML = `
    <div class="min-h-screen bg-slate-900 text-slate-100">
      <!-- Header with back button -->
      <div class="bg-slate-800 border-b border-slate-700 p-4 flex gap-4">
        <button id="btn-back" class="px-4 py-2 bg-slate-700 hover:bg-slate-600 rounded font-semibold">
          ← Back to Dashboard
        </button>
      </div>

      <!-- Main content -->
      <div class="max-w-6xl mx-auto p-4">
        <h1 class="text-3xl font-bold text-cyan-300 mb-6">${patient.name}</h1>

        <!-- Vitals Grid -->
        <div class="grid grid-cols-4 gap-4 mb-6">
          <div class="bg-slate-800 border border-slate-700 p-4 rounded">
            <div class="text-sm text-slate-400">Heart Rate</div>
            <div class="text-4xl font-bold text-red-400">${patient.vitals.hr}</div>
            <div class="text-xs text-slate-500">bpm</div>
          </div>
          <div class="bg-slate-800 border border-slate-700 p-4 rounded">
            <div class="text-sm text-slate-400">Respiratory Rate</div>
            <div class="text-4xl font-bold text-orange-400">${patient.vitals.rr}</div>
            <div class="text-xs text-slate-500">breaths/min</div>
          </div>
          <div class="bg-slate-800 border border-slate-700 p-4 rounded">
            <div class="text-sm text-slate-400">Oxygen Saturation</div>
            <div class="text-4xl font-bold text-cyan-400">${patient.vitals.spo2}</div>
            <div class="text-xs text-slate-500">%</div>
          </div>
          <div class="bg-slate-800 border border-slate-700 p-4 rounded">
            <div class="text-sm text-slate-400">Temperature</div>
            <div class="text-4xl font-bold text-yellow-400">${patient.vitals.temp.toFixed(1)}</div>
            <div class="text-xs text-slate-500">°C</div>
          </div>
        </div>

        <!-- Clinical Actions (if permitted) -->
        ${caps.canChartActions ? `
          <div class="bg-slate-800 border border-cyan-500/30 rounded-lg p-6 mb-6">
            <h2 class="text-xl font-bold text-cyan-300 mb-4">Clinical Documentation</h2>
            <textarea
              id="note-input"
              class="w-full bg-slate-700 border border-slate-600 rounded p-3 text-slate-100 placeholder-slate-500 focus:border-cyan-500 focus:outline-none mb-3"
              rows="3"
              placeholder="Add clinical notes..."
            ></textarea>
            <div class="flex gap-3">
              <button id="btn-add-note" class="flex-1 py-2 bg-cyan-600 hover:bg-cyan-500 rounded text-white font-semibold">
                Add Note
              </button>
              <button id="btn-suggest-sbar" class="flex-1 py-2 bg-blue-600 hover:bg-blue-500 rounded text-white font-semibold">
                Generate SBAR
              </button>
            </div>
          </div>
        ` : ''}

        <!-- Chart -->
        <div id="chart-area" class="bg-slate-800 border border-slate-700 rounded-lg p-6">
          <h2 class="text-xl font-bold text-cyan-300 mb-4">Clinical Chart</h2>
          <div id="chart-entries" class="max-h-96 overflow-y-auto space-y-2">
            <p class="text-slate-500">No chart entries yet</p>
          </div>
        </div>
      </div>
    </div>
  `;

  document.getElementById('btn-back')?.addEventListener('click', () => {
    renderDashboard();
  });

  if (caps.canChartActions) {
    document.getElementById('btn-add-note')?.addEventListener('click', () => {
      const noteInput = document.getElementById('note-input') as HTMLTextAreaElement;
      if (noteInput && noteInput.value.trim()) {
        const entry: ChartEntry = {
          timestamp: new Date(),
          type: 'note',
          content: noteInput.value,
          nurse: state.currentStaffName
        };
        store.addChartEntry(entry);
        noteInput.value = '';
        renderChartEntries();
      }
    });

    document.getElementById('btn-suggest-sbar')?.addEventListener('click', async () => {
      try {
        const scaffold = await apiClient.generateScaffold(patient.id);
        const entry: ChartEntry = {
          timestamp: new Date(),
          type: 'observation',
          content: `SBAR:\nS: ${scaffold.sbar.situation}\nB: ${scaffold.sbar.background}\nA: ${scaffold.sbar.assessment}\nR: ${scaffold.sbar.recommendation}`,
          nurse: state.currentStaffName
        };
        store.addChartEntry(entry);
        renderChartEntries();
      } catch (error) {
        showError(`Failed to generate SBAR: ${error}`);
      }
    });
  }

  function renderChartEntries() {
    const entries = store.getChartEntries();
    const chartArea = document.getElementById('chart-entries');
    if (chartArea) {
      if (entries.length === 0) {
        chartArea.innerHTML = '<p class="text-slate-500">No chart entries yet</p>';
      } else {
        chartArea.innerHTML = entries.map(entry => `
          <div class="bg-slate-700 border border-slate-600 rounded p-3">
            <div class="flex justify-between items-start mb-2">
              <span class="text-xs font-semibold text-cyan-400 uppercase">${entry.type}</span>
              <span class="text-xs text-slate-500">${entry.timestamp.toLocaleTimeString()}</span>
            </div>
            <p class="text-slate-300 text-sm whitespace-pre-wrap">${entry.content}</p>
            <div class="text-xs text-slate-500 mt-2">by ${entry.nurse}</div>
          </div>
        `).join('');
      }
    }
  }

  renderChartEntries();
}

function setupAutoRefresh() {
  setInterval(async () => {
    const state = store.getState();
    if (state.viewMode === 'dashboard' && state.patients.length > 0) {
      loadAndRenderPatients();
    }
  }, 5000); // Refresh every 5 seconds
}

function showError(message: string) {
  const container = document.getElementById('system-log');
  if (container) {
    const entry = document.createElement('div');
    entry.className = 'text-red-400 border-t border-slate-700 pt-1 px-2 py-1';
    entry.textContent = `[ERROR] ${message}`;
    container.appendChild(entry);
    container.scrollTop = container.scrollHeight;
  }
}

function showConnectionError() {
  app.innerHTML = `
    <div class="min-h-screen bg-slate-900 flex items-center justify-center">
      <div class="text-center">
        <div class="text-4xl font-bold text-red-400 mb-4">Connection Error</div>
        <p class="text-slate-400 mb-4">Could not connect to backend server at http://localhost:8080</p>
        <p class="text-slate-500 text-sm">Make sure the server is running:</p>
        <code class="block bg-slate-800 p-4 rounded mt-4 text-cyan-400">./build/optic-trigeminal</code>
        <button
          onclick="window.location.reload()"
          class="mt-8 px-6 py-2 bg-cyan-600 hover:bg-cyan-500 text-white rounded font-semibold"
        >
          Retry Connection
        </button>
      </div>
    </div>
  `;
}

// ============================================================================
// START APPLICATION
// ============================================================================

initializeApp();

// Export for testing
export { renderDashboard, renderPatientDetail };
