// ============================================================================
// TYPE DEFINITIONS - OpticTrigeminal API & Domain Models
// ============================================================================

// AUTH & ROLES ===============================================================

export type Role = 'rn' | 'charge_nurse' | 'provider' | 'admin' | 'it';

export interface RoleCapabilities {
  displayName: string;
  canViewVitals: boolean;
  canChartActions: boolean;
  canAddNotes: boolean;
  canAcceptRecommendations: boolean;
  canViewOrders: boolean;
  canAcknowledgeOrders: boolean;
  canAdmitPatient: boolean;
  canDischargePatient: boolean;
  canAssignPatients: boolean;
  canViewAllPatients: boolean;
  canOverrideAlerts: boolean;
  canViewAnalytics: boolean;
  canAccessAdmin: boolean;
}

export interface AuthSession {
  signedIn: boolean;
  currentRole: Role | null;
  currentStaffName: string;
  staffId: string;
  sessionStart: Date;
}

// PATIENT DATA ===============================================================

export interface Vitals {
  hr: number;
  rr: number;
  spo2: number;
  bp_sys: number;
  bp_dia: number;
  temp: number;
  is_crisis: boolean;
  crisis_type?: string;
  drift_variance: number;
}

export interface Patient {
  id: number;
  name: string;
  mrn: string;
  room: string;
  admission_diagnosis: string;
  acuity_score: number;
  vitals: Vitals;
  hr_history: number[];
  spo2_history: number[];
  temp_history: number[];
  nurse_notes: string;
}

export interface PatientObservation {
  patient_id: number;
  observation_type: 'trend' | 'threshold' | 'pattern' | 'correlation';
  severity: 'info' | 'warning' | 'critical';
  description: string;
  rationale: string;
  confidence: number;
  suggested_actions: string[];
  requires_nurse_attention: boolean;
  timestamp: Date;
}

// CLINICAL CHARTING ==========================================================

export interface ChartEntry {
  timestamp: Date;
  type: 'intervention' | 'note' | 'observation' | 'vital';
  content: string;
  nurse: string;
  entry_number?: number;
}

export interface SBARScaffold {
  situation: string;
  background: string;
  assessment: string;
  recommendation: string;
}

export interface SBARResponse {
  sbar: SBARScaffold;
  confidence: number;
  priority: 'LOW' | 'MEDIUM' | 'HIGH' | 'CRITICAL';
  suggested_actions: string[];
  timestamp: Date;
}

export interface ClinicalAction {
  patient_id: number;
  action: string;
  passcode_validated: boolean;
  nurse_id: string;
  nurse_name: string;
  timestamp?: Date;
}

export interface ClinicalActionResponse {
  status: string;
  action_id: string;
  patient_id: number;
  action: string;
  nurse: string;
  timestamp: Date;
  chart_entry: ChartEntry;
  acmk_learning: {
    recorded: boolean;
    confidence_impact: number;
  };
}

// INFERENCE & REASONING ======================================================

export interface InferenceRequest {
  prompt: string;
  max_tokens?: number;
}

export interface InferenceResponse {
  prompt: string;
  response: string;
  type: string;
  timestamp: Date;
  confidence: number;
  related_concepts: string[];
  reasoning_steps?: string[];
}

export interface SystemStatus {
  status: 'ready' | 'initializing' | 'error';
  vocab_size: number;
  graph_nodes: number;
  training_records: number;
  episodic_memory: number;
  uptime_ms: number;
  inference_latency_ms: number;
  last_inference: Date;
  embedding_quality: number;
  safety_precision: number;
  domain_accuracy: {
    math: number;
    logic: number;
    causality: number;
  };
  multimodal_fusion_quality: number;
  process_count: number;
  load_level: 'IDLE' | 'LOW' | 'MEDIUM' | 'HIGH' | 'CRITICAL';
}

// TRAINING SCENARIOS =========================================================

export interface TrainingScenario {
  id: string;
  title: string;
  category: string;
  difficulty: 'beginner' | 'intermediate' | 'advanced';
  duration_min: number;
  learning_objectives: string[];
}

export interface TrainingSession {
  training_session_id: string;
  status: 'idle' | 'running' | 'paused' | 'completed' | 'failed';
  scenario: {
    id: string;
    title: string;
    difficulty: string;
    objectives: string[];
  };
  patient: Patient;
  elapsed_ms: number;
  actions_taken: number;
  score: number;
  timestamp: Date;
}

export interface TrainingStatus {
  training_session_id: string;
  status: string;
  elapsed_ms: number;
  scenario: string;
  patient_id: number;
  current_vitals: Vitals;
  actions_taken: Array<{
    time_ms: number;
    action: string;
    effectiveness: number;
  }>;
  score: number;
  feedback: string;
  time_remaining_ms: number;
}

export interface TrainingAction {
  action: string;
  parameters?: Record<string, any>;
}

export interface TrainingActionResponse {
  training_session_id: string;
  action: string;
  status: string;
  effectiveness: number;
  patient_response: {
    spo2_change: number;
    hr_change: number;
    feedback: string;
  };
  score_delta: number;
  cumulative_score: number;
  timestamp: Date;
}

export interface TrainingReport {
  training_session_id: string;
  status: string;
  scenario: string;
  duration_ms: number;
  final_score: number;
  performance: {
    assessment: string;
    time_to_intervention: string;
    intervention_selection: string;
    escalation: string;
  };
  learning_summary: string[];
  improvement_areas: string[];
  transcript: Array<{
    time: string;
    action: string;
    score: number;
  }>;
  timestamp: Date;
}

// UI & STATE ==================================================================

export interface AppState {
  // Auth
  signedIn: boolean;
  currentRole: Role | null;
  currentStaffName: string;
  selectedRole?: Role;
  
  // Patient Data
  patients: Patient[];
  selectedPatientId: number | null;
  
  // Clinical
  chartEntries: ChartEntry[];
  observations: PatientObservation[];
  
  // Training
  trainingActive: boolean;
  currentTrainingSession: TrainingSession | null;
  trainingScenarios: TrainingScenario[];
  
  // UI
  viewMode: 'dashboard' | 'detail' | 'training';
  showPasscodeModal: boolean;
  showSignInModal: boolean;
  showAdmissionsModal: boolean;
  
  // Audit
  auditLog: AuditEntry[];
}

export interface AuditEntry {
  timestamp: Date;
  action: string;
  userId: string;
  role: Role;
  details: Record<string, any>;
}

// API RESPONSES ===============================================================

export interface ApiResponse<T = any> {
  status: 'success' | 'error';
  data?: T;
  error?: {
    code: string;
    message: string;
    details?: Record<string, any>;
  };
  timestamp: Date;
}

export interface HealthCheckResponse {
  status: 'healthy' | 'degraded' | 'unhealthy';
  timestamp: Date;
}

// WASM INTEGRATION ============================================================

export interface WasmModule {
  memory: WebAssembly.Memory;
  exports: {
    wasm_infer: (prompt_ptr: number, max_tokens: number) => number;
    wasm_get_patient_vitals: (patient_id: number) => number;
    wasm_log_action: (action_json_ptr: number) => number;
    malloc: (size: number) => number;
    free: (ptr: number) => void;
  };
}

// VALIDATION ==================================================================

export interface ValidationResult {
  valid: boolean;
  errors: string[];
}

export function validateVitals(vitals: any): ValidationResult {
  const errors: string[] = [];
  
  if (!Number.isInteger(vitals.hr) || vitals.hr < 0 || vitals.hr > 220) {
    errors.push('Heart rate must be between 0-220 bpm');
  }
  if (!Number.isInteger(vitals.rr) || vitals.rr < 0 || vitals.rr > 60) {
    errors.push('Respiratory rate must be between 0-60');
  }
  if (!Number.isInteger(vitals.spo2) || vitals.spo2 < 0 || vitals.spo2 > 100) {
    errors.push('SpO2 must be between 0-100%');
  }
  
  return {
    valid: errors.length === 0,
    errors
  };
}

export function validateChartEntry(entry: any): ValidationResult {
  const errors: string[] = [];
  
  if (!entry.timestamp) {
    errors.push('Chart entry must have timestamp');
  }
  if (!['intervention', 'note', 'observation', 'vital'].includes(entry.type)) {
    errors.push('Invalid chart entry type');
  }
  if (!entry.content || entry.content.trim().length === 0) {
    errors.push('Chart entry content is required');
  }
  
  return {
    valid: errors.length === 0,
    errors
  };
}
