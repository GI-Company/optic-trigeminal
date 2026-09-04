console.log('main-refactored.ts loading...');

import { apiClient } from '@api/client';
import { initializeWasm } from '@api/wasm-bridge';
import { store } from '@store/state';
import { router } from '@utils/router';
import { showToast } from '@utils/ui-helpers';
import { SignInScreen, type SignInResult } from '@components/SignInScreen';
import { ClinicalDashboard } from '@components/ClinicalDashboard';
import { AdminDashboard } from '@components/AdminDashboard';
import { SystemDashboard } from '@components/SystemDashboard';
import { InstructorDashboard } from '@components/InstructorDashboard';
import { PatientDetail, type VitalKey } from '@components/PatientDetail';
import { VitalHistoryPanel } from '@components/VitalHistoryPanel';
import { TrainingMode } from '@components/TrainingMode';
import { TrainingDebrief } from '@components/TrainingDebrief';
import { ScenarioSelector } from '@components/ScenarioSelector';
import { ConnectionError } from '@components/ConnectionError';
import { ReasoningTraceView } from '@components/ReasoningTrace';
import type { Role, RoleCapabilities, ChartEntry } from '@api/types';
import './styles/index.css';

console.log('main-refactored.ts imports complete');
const app = document.getElementById('app')!;
console.log('App container found:', app);

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
  },
  instructor: {
    // Never rendered by ClinicalDashboard (renderDashboard() below routes
    // 'instructor' straight to InstructorDashboard, which doesn't take
    // roleCapabilities at all) -- this entry exists only so the Record<Role,
    // RoleCapabilities> type stays total. Kept all-false/oversight-shaped
    // to match what the server-side role actually grants, in case anything
    // ever reads it by mistake.
    displayName: 'Instructor',
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
    canAccessAdmin: false
  }
};

async function initialize(): Promise<void> {
  try {
    console.log('OpticTrigeminal: Starting initialization...');
    
    const isHealthy = await apiClient.testConnection();
    console.log('Backend health check:', isHealthy);
    
    if (!isHealthy) {
      console.error('Backend not healthy, but continuing with WASM fallback...');
    }

    console.log('Initializing WASM bridge...');
    try {
      await initializeWasm();
      console.log('WASM bridge initialized successfully');
    } catch (wasmError) {
      // Non-fatal: apiClient falls back to network requests whenever the
      // WASM bridge isn't ready (see getWasmBridge() usage in api/client.ts),
      // so a failed/timed-out edge engine shouldn't block the whole app from
      // loading. This is also the safety net for a hung worker: loadModule()
      // now carries its own timeout, so this await is guaranteed to settle
      // instead of hanging the UI on the loading screen forever.
      console.error('WASM bridge failed to initialize, continuing without edge inference:', wasmError);
    }

    setupRoutes();
    console.log('Routes registered, navigating to signin...');
    router.navigate('/signin');
    setupAutoRefresh();
    
    console.log('Initialization complete');

  } catch (error) {
    console.error('Initialization failed:', error);
    showConnectionError();
  }
}

function setupRoutes(): void {
  console.log('Setting up routes...');
  router.register('/signin', renderSignIn);
  router.register('/dashboard', renderDashboard);
  router.register('/patient', renderPatientDetail);
  router.register('/training', renderTrainingMode);
  router.register('/reasoning', renderReasoningTrace);
  router.register('/debrief', renderTrainingDebrief);
  console.log('Routes setup complete');
}

function renderSignIn(): void {
  console.log('Rendering SignIn screen...');

  const signIn = new SignInScreen({
    onSignIn: async (staffId: string, password: string) => {
      const result = await apiClient.signIn(staffId, password);
      return result;
    },
    onSignedIn: async (result: SignInResult) => {
      console.log('Signed in as:', result.role, result.staffName);
      store.signIn(result.role, result.staffName);

      // Every ACMK-OT route requires a session; initialize one now so the
      // dashboards below have a session_id to show/use. Defaults to
      // simulation mode -- see docs/API_REFERENCE.md.
      try {
        const session = await apiClient.initAcmkSession('simulation');
        store.setAcmkSession(session.session_id, session.mode as 'simulation' | 'real_world');
      } catch (e) {
        console.warn('ACMK session init failed (non-fatal):', e);
      }

      // The initial fetch in initialize() below runs before sign-in, with
      // no auth token yet -- /api/training/list requires one (fixed
      // earlier this session), so that first call always 401's and leaves
      // trainingScenarios permanently empty. Nothing ever refetched it
      // after a real token existed, so "Enter Training Mode" opened a
      // scenario picker with nothing in it, for every single sign-in.
      try {
        const scenarios = await apiClient.listTrainingScenarios();
        store.setTrainingScenarios(scenarios);
      } catch (e) {
        console.warn('Failed to load training scenarios after sign-in:', e);
      }

      await loadPatients();
      router.navigate('/dashboard');
    }
  });

  signIn.mount(app);
  console.log('SignIn screen mounted');
}

async function loadPatients(): Promise<void> {
  // Real, server-simulated patients (src/server/http_server.cpp:
  // handle_patients), filtered server-side to whatever this signed-in
  // user is actually assigned to -- previously this always used
  // web/src/store/mockData.ts's fabricated roster, completely disconnected
  // from the same `sim` instance every other clinical endpoint reads from,
  // so e.g. a chart note or SBAR generated here referenced a patient ID
  // that meant something different (or nothing) to the backend.
  try {
    const patients = await apiClient.getPatients();
    store.setPatients(patients);
  } catch (e) {
    console.warn('Failed to load patient roster from server:', e);
    store.setPatients([]);
  }
}

function renderDashboard(): void {
  const state = store.getState();
  if (!state.signedIn) {
    router.navigate('/signin');
    return;
  }

  const role = state.currentRole!;
  const onSignOut = () => {
    apiClient.signOut();
    store.signOut();
    router.navigate('/signin');
  };

  if (role === 'admin') {
    const dashboard = new AdminDashboard({
      currentStaffName: state.currentStaffName,
      patients: state.patients,
      acmkSessionId: state.acmkSessionId,
      onSignOut
    });
    dashboard.mount(app);
  } else if (role === 'it') {
    const dashboard = new SystemDashboard({
      currentStaffName: state.currentStaffName,
      onSignOut
    });
    dashboard.mount(app);
  } else if (role === 'instructor') {
    const dashboard = new InstructorDashboard({
      currentStaffName: state.currentStaffName,
      onSignOut
    });
    dashboard.mount(app);
  } else {
    // rn, charge_nurse, provider
    const dashboard = new ClinicalDashboard({
      currentStaffName: state.currentStaffName,
      currentRole: role,
      roleCapabilities: roleCapabilities[role],
      patients: state.patients,
      trainingScenarios: state.trainingScenarios,
      acmkSessionId: state.acmkSessionId,
      acmkMode: state.acmkMode,
      onSignOut,
      onSelectPatient: (patientId: number) => {
        store.selectPatient(patientId);
        router.navigate('/patient');
      },
      onStartTraining: renderScenarioSelector,
      onOpenReasoningTrace: () => router.navigate('/reasoning'),
      // renderDashboard() below fires loadPatients() without awaiting it
      // (kept non-blocking for the normal render path), so calling it alone
      // here would re-render with the same stale roster that was just
      // admitted/discharged/assigned against, a beat before the fetch that
      // would actually show the change resolves. Await the fetch first.
      onPatientsChanged: async () => {
        await loadPatients();
        renderDashboard();
      }
    });
    dashboard.mount(app);
    mountedClinicalDashboard = dashboard;
  }

  loadPatients();
}

// Holds the mounted ClinicalDashboard (rn/charge_nurse/provider role only --
// the other role branches above don't show a patient grid) so
// setupAutoRefresh's poll can patch cards in place via updatePatients()
// instead of re-mounting the whole dashboard every 5s. Cleared implicitly
// by simply going stale when a different role's dashboard or another route
// mounts -- setupAutoRefresh only ever calls into it while router.isOn('/dashboard'),
// and the next renderDashboard() call always reassigns it before anything
// would read a dangling reference.
let mountedClinicalDashboard: ClinicalDashboard | null = null;

function renderPatientDetail(): void {
  const state = store.getState();
  const patient = store.getSelectedPatient();

  if (!patient) {
    router.navigate('/dashboard');
    return;
  }

  // Chart entries are now real, server-persisted data (see
  // handle_chart / chart_log.ndjson) instead of only ever living in
  // browser-local state -- pull the real history before rendering so a
  // freshly-opened patient shows what was actually charted, not an empty
  // list that resets on every refresh.
  apiClient.getChartEntriesRemote(patient.id).then(remoteEntries => {
    store.clearChart();
    for (const e of remoteEntries) {
      store.addChartEntry({
        timestamp: new Date(e.timestamp * 1000),
        type: (e.type as ChartEntry['type']) || 'note',
        content: e.content,
        nurse: e.nurse
      });
    }
    detail.refreshChart(store.getChartEntries());
  }).catch(err => console.warn('Failed to load chart history:', err));

  const detail = new PatientDetail({
    patient,
    chartEntries: store.getChartEntries(),
    currentStaffName: state.currentStaffName,
    roleCapabilities: roleCapabilities[state.currentRole!],
    onBack: () => {
      mountedPatientDetail = null;
      router.navigate('/dashboard');
    },
    onAddNote: async (content: string) => {
      try {
        await apiClient.addChartEntryRemote(patient.id, 'note', content);
        store.addChartEntry({
          timestamp: new Date(),
          type: 'note',
          content,
          nurse: state.currentStaffName
        });
        detail.refreshChart(store.getChartEntries());
        showToast('Note added', 'success');
      } catch (error) {
        showToast(`Failed to add note: ${error}`, 'error');
      }
    },
    onGenerateSBAR: async () => {
      try {
        const scaffold = await apiClient.generateScaffold(patient.id, state.acmkSessionId || undefined);
        const content = `SBAR:\nS: ${scaffold.sbar.situation}\nB: ${scaffold.sbar.background}\nA: ${scaffold.sbar.assessment}\nR: ${scaffold.sbar.recommendation}`;
        await apiClient.addChartEntryRemote(patient.id, 'observation', content);
        store.addChartEntry({
          timestamp: new Date(),
          type: 'observation',
          content,
          nurse: state.currentStaffName
        });
        detail.refreshChart(store.getChartEntries());
        showToast('SBAR generated', 'success');
      } catch (error) {
        showToast(`Failed to generate SBAR: ${error}`, 'error');
      }
    },
    onVitalClick: async (vital: VitalKey) => {
      const root = document.getElementById('modal-root');
      if (!root) return;
      let panel: VitalHistoryPanel | null = null;
      try {
        // Fresh, not cached -- observations are computed live per-request
        // (ClinicalAnalyzer::analyze_patient), so this is the current state.
        const observations = await apiClient.getPatientObservations(patient.id);
        panel = new VitalHistoryPanel({
          vital,
          patient: store.getSelectedPatient() || patient,
          observations,
          chartEntries: store.getChartEntries(),
          onClose: () => { panel?.unmount(); }
        });
        panel.mount(root);
      } catch (error) {
        showToast(`Failed to load vital history: ${error}`, 'error');
      }
    }
  });

  detail.mount(app);
  mountedPatientDetail = detail;
}

// Holds the mounted PatientDetail instance so setupAutoRefresh's poll can
// patch just the vital numbers (updateVitals) instead of re-running
// renderPatientDetail() wholesale -- see updateVitals's own comment in
// PatientDetail.ts for why a full remount isn't the fix here (it would
// re-fire the page's fade-in animation and blow away in-progress state
// like an unsent note draft, the same flash TrainingMode.updateLiveData
// was already written to avoid).
let mountedPatientDetail: PatientDetail | null = null;

function renderReasoningTrace(): void {
  const state = store.getState();
  if (!state.signedIn || !state.acmkSessionId) {
    router.navigate('/dashboard');
    return;
  }

  const view = new ReasoningTraceView({
    sessionId: state.acmkSessionId,
    roleCapabilities: roleCapabilities[state.currentRole!],
    onBack: () => router.navigate('/dashboard')
  });
  view.mount(app);
}

// ScenarioRuntime (src/clinical/training_scenario.cpp) only advances
// elapsed_time_sec_ -- and therefore the scripted timeline that actually
// moves vitals away from baseline (apply_timeline_events) -- inside
// tick(). Nothing here ever called POST /api/training/tick, so a session
// sat frozen at t=0 for its whole duration: vitals never got bad enough to
// make a vitals-gated action like "apply IV fluids" ever score as
// on-time, no matter when you pressed it. This polls the real endpoint
// while a session is active so the scenario actually progresses.
let trainingTickInterval: ReturnType<typeof setInterval> | null = null;
const TRAINING_TICK_REAL_MS = 4000;
const TRAINING_TICK_SIM_SECONDS = 60;

function startTrainingTickLoop(): void {
  stopTrainingTickLoop();
  trainingTickInterval = setInterval(async () => {
    const state = store.getState();
    const session = state.currentTrainingSession;
    if (!session) { stopTrainingTickLoop(); return; }

    try {
      const tick = await apiClient.advanceTrainingTick(TRAINING_TICK_SIM_SECONDS);
      const updated = {
        ...session,
        elapsed_ms: (tick.elapsed_seconds ?? (session.elapsed_ms / 1000) + TRAINING_TICK_SIM_SECONDS) * 1000,
        patient: {
          ...session.patient,
          vitals: {
            ...session.patient.vitals,
            hr: tick.vitals?.hr ?? session.patient.vitals.hr,
            spo2: tick.vitals?.spo2 ?? session.patient.vitals.spo2,
            bp_sys: tick.vitals?.bp_sys ?? session.patient.vitals.bp_sys
          }
        },
        dominant_driver: tick.dominant_driver ?? session.dominant_driver
      };
      store.updateTrainingSession(updated);

      // Patch just the changed numbers rather than a full re-render --
      // see TrainingMode.updateLiveData for why re-rendering the whole
      // screen every 4 seconds caused a visible flash on every tick.
      if (router.isOn('/training')) mountedTrainingMode?.updateLiveData(updated);

      if (tick.failure_conditions_triggered?.length) {
        showToast(`Deterioration: ${tick.failure_conditions_triggered.join(', ')}`, 'error');
      }
      if (tick.escalation_reason && tick.escalation_reason !== 'None') {
        showToast(tick.escalation_reason, 'error');
      }
    } catch (error) {
      // A tick failing (e.g. session already ended server-side) shouldn't
      // spam the user every 4s -- just stop polling.
      stopTrainingTickLoop();
    }
  }, TRAINING_TICK_REAL_MS);
}

function stopTrainingTickLoop(): void {
  if (trainingTickInterval !== null) {
    clearInterval(trainingTickInterval);
    trainingTickInterval = null;
  }
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
        startTrainingTickLoop();
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

// Holds the mounted TrainingMode instance so ticks/actions can patch just
// the numbers that changed (updateLiveData) instead of tearing down and
// rebuilding the whole screen -- see TrainingMode.updateLiveData for why a
// full remount on every 4s tick was causing a visible flash on every update.
let mountedTrainingMode: TrainingMode | null = null;

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
        const updated = {
          ...store.getState().currentTrainingSession!,
          score: response.cumulative_score,
          elapsed_ms: session.elapsed_ms + 1000
        };
        store.updateTrainingSession(updated);
        mountedTrainingMode?.updateLiveData(updated);
        // response.patient_response.feedback is the actual clinical
        // rationale from ScenarioRuntime::evaluate_action_correctness
        // (e.g. "Premature action - BP not critically low yet") -- showing
        // a generic "Action executed" here regardless of outcome is what
        // made the scoring feel arbitrary/fake even after it started being
        // real; the whole point is to see *why*.
        const isCorrect = response.status === 'correct';
        showToast(
          response.patient_response?.feedback || `Action executed: ${action}`,
          isCorrect ? 'success' : 'error'
        );
      } catch (error) {
        showToast(`Action failed: ${error}`, 'error');
      }
    },
    onEnd: async () => {
      stopTrainingTickLoop();
      mountedTrainingMode = null;
      try {
        // Was: discard the response, toast "Training session ended", bounce
        // to dashboard. The server was already computing a real per-action
        // transcript, missed-critical-window list, and score breakdown
        // (handle_training_end in src/server/http_server.cpp) -- routing to
        // a debrief screen instead of throwing the report away is the whole
        // point of the fix.
        const report = await apiClient.endTraining(false);
        store.endTraining(report);
        router.navigate('/debrief');
      } catch (error) {
        showToast(`Failed to end training: ${error}`, 'error');
        store.endTraining();
        router.navigate('/dashboard');
      }
    }
  });

  training.mount(app);
  mountedTrainingMode = training;
}

function renderTrainingDebrief(): void {
  const state = store.getState();
  if (!state.lastTrainingReport) {
    router.navigate('/dashboard');
    return;
  }

  const debrief = new TrainingDebrief({
    report: state.lastTrainingReport,
    onDone: () => {
      router.navigate('/dashboard');
    },
    onGenerateNoteDraft: async (sessionId: string) => {
      try {
        const draft = await apiClient.generateTrainingNoteDraft(sessionId);
        return draft.draft_content;
      } catch (err: any) {
        showToast(err?.message || 'Failed to generate note draft', 'error');
        throw err;
      }
    },
    onSignNote: async (sessionId: string, content: string, wasEdited: boolean) => {
      try {
        const result = await apiClient.signTrainingNote(sessionId, content, wasEdited);
        showToast('Note signed and submitted', 'success');
        return new Date(result.signed_at).toLocaleTimeString();
      } catch (err: any) {
        showToast(err?.message || 'Failed to sign note', 'error');
        throw err;
      }
    }
  });
  debrief.mount(app);
}

function showConnectionError(): void {
  const error = new ConnectionError();
  error.mount(app);
}

function setupAutoRefresh(): void {
  setInterval(async () => {
    if (router.isOn('/dashboard')) {
      // Same staleness gap patient-detail had before updateVitals: this
      // used to only update the store, with nothing consuming it -- every
      // number on the mounted dashboard (vitals, score, alerts, gestalt
      // strip) silently froze at whatever it was on first render.
      // updatePatients() patches cards in place; a no-op for any other
      // role's dashboard (see mountedClinicalDashboard's own comment).
      loadPatients()
        .then(() => mountedClinicalDashboard?.updatePatients(store.getState().patients))
        .catch(console.error);
    } else if (router.isOn('/patient') && mountedPatientDetail) {
      // Patient detail used to have no live-refresh path at all -- vitals
      // (and anything read from store.getSelectedPatient() while here, e.g.
      // the counterfactual-fork panel's snapshot/attribution data) froze at
      // whatever they were the moment the page opened. loadPatients()
      // refreshes the whole roster in the store; updateVitals then patches
      // just this patient's numbers rather than remounting the page.
      try {
        await loadPatients();
        const updated = store.getSelectedPatient();
        if (updated) mountedPatientDetail?.updateVitals(updated);
      } catch (e) {
        console.error('Failed to refresh patient detail:', e);
      }
    }
  }, 5000);
}

initialize();

export { renderDashboard, renderPatientDetail, router };
