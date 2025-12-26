import type {
  AppState,
  Role,
  Patient,
  ChartEntry,
  PatientObservation,
  TrainingSession,
  TrainingScenario,
  AuditEntry
} from '@api/types';

/**
 * Global Application State
 * Central store for all app data
 */
class StateManager {
  private state: AppState = {
    // Auth
    signedIn: false,
    currentRole: null,
    currentStaffName: '',
    
    // Patient Data
    patients: [],
    selectedPatientId: null,
    
    // Clinical
    chartEntries: [],
    observations: [],
    
    // Training
    trainingActive: false,
    currentTrainingSession: null,
    trainingScenarios: [],
    
    // UI
    viewMode: 'dashboard',
    showPasscodeModal: false,
    showSignInModal: true,
    showAdmissionsModal: false,
    
    // Audit
    auditLog: []
  };

  private listeners: Set<(state: AppState) => void> = new Set();

  /**
   * Get current state (immutable copy)
   */
  getState(): Readonly<AppState> {
    return JSON.parse(JSON.stringify(this.state));
  }

  /**
   * Update state (partial update)
   */
  setState(partial: Partial<AppState>): void {
    this.state = { ...this.state, ...partial };
    this.notifyListeners();
  }

  /**
   * Subscribe to state changes
   */
  subscribe(listener: (state: AppState) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  /**
   * Notify all listeners of state change
   */
  private notifyListeners(): void {
    this.listeners.forEach(listener => listener(this.getState()));
  }

  // =========================================================================
  // AUTH METHODS
  // =========================================================================

  signIn(role: Role, staffName: string): void {
    this.state.signedIn = true;
    this.state.currentRole = role;
    this.state.currentStaffName = staffName;
    this.state.showSignInModal = false;
    
    this.logAuditAction('SIGN_IN', `Staff signed in as ${staffName}`);
    this.notifyListeners();
  }

  signOut(): void {
    this.logAuditAction('SIGN_OUT', `${this.state.currentStaffName} signed out`);
    
    this.state.signedIn = false;
    this.state.currentRole = null;
    this.state.currentStaffName = '';
    this.state.selectedPatientId = null;
    this.state.showSignInModal = true;
    
    this.notifyListeners();
  }

  switchRole(newRole: Role, displayName: string): void {
    if (!this.state.signedIn) return;
    
    const oldRole = this.state.currentRole;
    this.state.currentRole = newRole;
    this.state.currentStaffName = displayName;
    
    this.logAuditAction(
      'ROLE_SWITCH',
      `Switched from ${oldRole} to ${newRole}`
    );
    this.notifyListeners();
  }

  // =========================================================================
  // PATIENT METHODS
  // =========================================================================

  setPatients(patients: Patient[]): void {
    this.state.patients = patients;
    this.notifyListeners();
  }

  updatePatient(patient: Patient): void {
    const idx = this.state.patients.findIndex(p => p.id === patient.id);
    if (idx >= 0) {
      this.state.patients[idx] = patient;
    } else {
      this.state.patients.push(patient);
    }
    this.notifyListeners();
  }

  selectPatient(patientId: number): void {
    this.state.selectedPatientId = patientId;
    this.state.viewMode = 'detail';
    
    this.logAuditAction(
      'PATIENT_SELECT',
      `Selected patient ${patientId}`
    );
    this.notifyListeners();
  }

  getSelectedPatient(): Patient | undefined {
    if (!this.state.selectedPatientId) return undefined;
    return this.state.patients.find(p => p.id === this.state.selectedPatientId);
  }

  // =========================================================================
  // CLINICAL CHARTING
  // =========================================================================

  addChartEntry(entry: ChartEntry): void {
    this.state.chartEntries.push(entry);
    this.notifyListeners();
  }

  clearChart(): void {
    this.state.chartEntries = [];
    this.notifyListeners();
  }

  getChartEntries(): ChartEntry[] {
    return [...this.state.chartEntries];
  }

  setObservations(observations: PatientObservation[]): void {
    this.state.observations = observations;
    this.notifyListeners();
  }

  addObservation(observation: PatientObservation): void {
    this.state.observations.push(observation);
    this.notifyListeners();
  }

  // =========================================================================
  // TRAINING METHODS
  // =========================================================================

  setTrainingScenarios(scenarios: TrainingScenario[]): void {
    this.state.trainingScenarios = scenarios;
    this.notifyListeners();
  }

  startTraining(session: TrainingSession): void {
    this.state.trainingActive = true;
    this.state.currentTrainingSession = session;
    this.state.viewMode = 'training';
    
    this.logAuditAction(
      'TRAINING_START',
      `Started training scenario ${session.scenario.id}`
    );
    this.notifyListeners();
  }

  updateTrainingSession(session: TrainingSession): void {
    this.state.currentTrainingSession = session;
    this.notifyListeners();
  }

  endTraining(): void {
    if (this.state.currentTrainingSession) {
      this.logAuditAction(
        'TRAINING_END',
        `Ended training with score ${this.state.currentTrainingSession.score}`
      );
    }
    
    this.state.trainingActive = false;
    this.state.currentTrainingSession = null;
    this.state.viewMode = 'dashboard';
    
    this.notifyListeners();
  }

  // =========================================================================
  // UI STATE
  // =========================================================================

  switchView(viewMode: 'dashboard' | 'detail' | 'training'): void {
    this.state.viewMode = viewMode;
    this.notifyListeners();
  }

  openPasscodeModal(): void {
    this.state.showPasscodeModal = true;
    this.notifyListeners();
  }

  closePasscodeModal(): void {
    this.state.showPasscodeModal = false;
    this.notifyListeners();
  }

  openSignInModal(): void {
    this.state.showSignInModal = true;
    this.notifyListeners();
  }

  closeSignInModal(): void {
    this.state.showSignInModal = false;
    this.notifyListeners();
  }

  toggleAdmissionsModal(): void {
    this.state.showAdmissionsModal = !this.state.showAdmissionsModal;
    this.notifyListeners();
  }

  // =========================================================================
  // AUDIT LOGGING
  // =========================================================================

  logAuditAction(action: string, details: string = ''): void {
    const entry: AuditEntry = {
      timestamp: new Date(),
      action,
      userId: this.state.currentStaffName || 'UNKNOWN',
      role: this.state.currentRole || 'it',
      details: { message: details }
    };
    
    this.state.auditLog.push(entry);
    
    // Keep only last 1000 entries
    if (this.state.auditLog.length > 1000) {
      this.state.auditLog = this.state.auditLog.slice(-1000);
    }
  }

  getAuditLog(): AuditEntry[] {
    return [...this.state.auditLog];
  }

  // =========================================================================
  // DEBUGGING
  // =========================================================================

  /**
   * Export full state for debugging
   */
  export(): AppState {
    return this.getState();
  }

  /**
   * Clear all state (for testing)
   */
  reset(): void {
    this.state = {
      signedIn: false,
      currentRole: null,
      currentStaffName: '',
      selectedRole: undefined,
      patients: [],
      selectedPatientId: null,
      chartEntries: [],
      observations: [],
      trainingActive: false,
      currentTrainingSession: null,
      trainingScenarios: [],
      viewMode: 'dashboard',
      showPasscodeModal: false,
      showSignInModal: true,
      showAdmissionsModal: false,
      auditLog: []
    };
    this.notifyListeners();
  }
}

/**
 * Global state instance
 */
export const store = new StateManager();

/**
 * Helper hook-like function for subscribing to state
 */
export function useAppState(listener: (state: AppState) => void): () => void {
  return store.subscribe(listener);
}
