import { apiClient } from '@api/client';
import { store } from '@store/state';
import { router } from '@utils/router';
import { showToast } from '@utils/ui-helpers';
import { SignInScreen } from '@components/SignInScreen';
import { Dashboard } from '@components/Dashboard';
import { PatientDetail } from '@components/PatientDetail';
import { TrainingMode } from '@components/TrainingMode';
import { ScenarioSelector } from '@components/ScenarioSelector';
import { ConnectionError } from '@components/ConnectionError';
import type { Patient, Role, RoleCapabilities, ChartEntry } from '@api/types';
import './styles/index.css';

const app = document.getElementById('app')!;

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

async function initialize(): Promise<void> {
  try {
    const isHealthy = await apiClient.testConnection();
    if (!isHealthy) {
      showConnectionError();
      return;
    }

    const scenarios = await apiClient.listTrainingScenarios();
    store.setTrainingScenarios(scenarios);

    setupRoutes();
    router.navigate('/signin');
    setupAutoRefresh();

  } catch (error) {
    console.error('Initialization failed:', error);
    showConnectionError();
  }
}

function setupRoutes(): void {
  router.register('/signin', renderSignIn);
  router.register('/dashboard', renderDashboard);
  router.register('/patient', renderPatientDetail);
  router.register('/training', renderTrainingMode);
}

function renderSignIn(): void {
  const roles: Role[] = ['rn', 'charge_nurse', 'provider', 'admin', 'it'];

  const signIn = new SignInScreen({
    roles,
    roleDisplayNames: Object.fromEntries(
      roles.map(role => [role, roleCapabilities[role].displayName])
    ) as Record<Role, string>,
    onSignIn: (role: Role) => {
      const caps = roleCapabilities[role];
      store.signIn(role, caps.displayName);
      loadPatients().then(() => {
        router.navigate('/dashboard');
      });
    }
  });

  signIn.mount(app);
}

async function loadPatients(): Promise<void> {
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
}

function renderDashboard(): void {
  const state = store.getState();
  if (!state.signedIn) {
    router.navigate('/signin');
    return;
  }

  const dashboard = new Dashboard({
    currentStaffName: state.currentStaffName,
    currentRole: state.currentRole!,
    roleCapabilities: roleCapabilities[state.currentRole!],
    patients: state.patients,
    trainingScenarios: state.trainingScenarios,
    onSignOut: () => {
      store.signOut();
      router.navigate('/signin');
    },
    onSelectPatient: (patientId: number) => {
      store.selectPatient(patientId);
      router.navigate('/patient');
    },
    onStartTraining: renderScenarioSelector
  });

  dashboard.mount(app);
  loadPatients();
}

function renderPatientDetail(): void {
  const state = store.getState();
  const patient = store.getSelectedPatient();

  if (!patient) {
    router.navigate('/dashboard');
    return;
  }

  const detail = new PatientDetail({
    patient,
    chartEntries: store.getChartEntries(),
    currentStaffName: state.currentStaffName,
    roleCapabilities: roleCapabilities[state.currentRole!],
    onBack: () => {
      router.navigate('/dashboard');
    },
    onAddNote: (content: string) => {
      const entry: ChartEntry = {
        timestamp: new Date(),
        type: 'note',
        content,
        nurse: state.currentStaffName
      };
      store.addChartEntry(entry);
      detail.refreshChart();
      showToast('Note added', 'success');
    },
    onGenerateSBAR: async () => {
      try {
        const scaffold = await apiClient.generateScaffold(patient.id);
        const entry: ChartEntry = {
          timestamp: new Date(),
          type: 'observation',
          content: `SBAR:\nS: ${scaffold.sbar.situation}\nB: ${scaffold.sbar.background}\nA: ${scaffold.sbar.assessment}\nR: ${scaffold.sbar.recommendation}`,
          nurse: state.currentStaffName
        };
        store.addChartEntry(entry);
        detail.refreshChart();
        showToast('SBAR generated', 'success');
      } catch (error) {
        showToast(`Failed to generate SBAR: ${error}`, 'error');
      }
    }
  });

  detail.mount(app);
}

function renderScenarioSelector(): void {
  const state = store.getState();
  const selector = new ScenarioSelector({
    scenarios: state.trainingScenarios,
    onSelect: async (scenarioId: string) => {
      try {
        const session = await apiClient.startTraining(
          scenarioId,
          state.currentRole || undefined
        );
        store.startTraining(session);
        selector.unmount();
        router.navigate('/training');
      } catch (error) {
        showToast(`Failed to start training: ${error}`, 'error');
      }
    },
    onCancel: () => {
      selector.unmount();
    }
  });

  selector.mount(document.body);
}

function renderTrainingMode(): void {
  const state = store.getState();
  const session = state.currentTrainingSession;

  if (!session) {
    router.navigate('/dashboard');
    return;
  }

  const training = new TrainingMode({
    session,
    onAction: async (action: string, params?: Record<string, any>) => {
      try {
        const response = await apiClient.executeTrainingAction({
          action,
          parameters: params
        });
        store.updateTrainingSession({
          ...session,
          score: response.cumulative_score,
          elapsed_ms: session.elapsed_ms + 1000
        });
        renderTrainingMode();
        showToast(`Action executed: ${action}`, 'success');
      } catch (error) {
        showToast(`Action failed: ${error}`, 'error');
      }
    },
    onEnd: async () => {
      try {
        await apiClient.endTraining(false);
        store.endTraining();
        showToast('Training session ended', 'success');
        router.navigate('/dashboard');
      } catch (error) {
        showToast(`Failed to end training: ${error}`, 'error');
      }
    }
  });

  training.mount(app);
}

function showConnectionError(): void {
  const error = new ConnectionError();
  error.mount(app);
}

function setupAutoRefresh(): void {
  setInterval(() => {
    if (router.isOn('/dashboard')) {
      loadPatients().catch(console.error);
    }
  }, 5000);
}

initialize();

export { renderDashboard, renderPatientDetail, router };
