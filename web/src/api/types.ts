// ============================================================================
// TYPE DEFINITIONS - ACMK-OT Frontend API & Domain Models
// ============================================================================

// COGNITIVE STATE ENUMS ======================================================

export enum CognitiveState {
  IDLE = 'IDLE',
  INGESTING = 'INGESTING',
  RESOLVING = 'RESOLVING',
  CONVERGING = 'CONVERGING',
  CONVERGED = 'CONVERGED'
}

export enum RiskPosture {
  LOW = 'LOW',
  MODERATE = 'MODERATE',
  ELEVATED = 'ELEVATED',
  CRITICAL = 'CRITICAL'
}

export enum ErrorClass {
  PERCEPTUAL_FAILURE = 'PERCEPTUAL_FAILURE',
  TEMPORAL_INCONSISTENCY = 'TEMPORAL_INCONSISTENCY',
  CONSTRAINT_CONFLICT = 'CONSTRAINT_CONFLICT',
  CONFIDENCE_COLLAPSE = 'CONFIDENCE_COLLAPSE',
  HUMAN_INTERFERENCE = 'HUMAN_INTERFERENCE'
}

// AUTH & ROLES (FHIR-Aligned) ================================================

// Matches the server's auth Role enum (include/auth_manager.h) exactly --
// this is the set of roles that can actually sign in and get a token. The
// broader ACMK::ClinicalRole vocabulary (rbac_fhir.h -- resident,
// rapid_response, informatics, quality, legal, executive, patient, ...) is
// a separate FHIR-scoping concept used server-side for data access, not a
// sign-in identity, so most of it doesn't belong in this union. 'instructor'
// is the one exception: it maps to ClinicalRole::EDUCATION_SIMULATION on the
// server, but it's also a real, separately-authenticated sign-in identity
// (manages class cohorts, no clinical access at all -- see CohortManager).
export type Role = 'rn' | 'charge_nurse' | 'provider' | 'admin' | 'it' | 'instructor';

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
  rr_history: number[];
  spo2_history: number[];
  temp_history: number[];
  // Unix seconds, one per sample -- history_timestamps[i] is when
  // hr_history[i]/rr_history[i]/spo2_history[i]/temp_history[i] were all
  // sampled (they're appended in lockstep server-side). Same convention as
  // ChartEntry.timestamp (that one's a Date; this is raw seconds -- multiply
  // by 1000 for a JS Date, matching how ChartEntry.timestamp is parsed).
  history_timestamps: number[];
  // Unix seconds, one per full resumable state snapshot (deeper ring buffer
  // than history_timestamps above -- see include/clinical_sim.h's
  // PatientSnapshot). These are the points a counterfactual fork can branch
  // from (POST /api/clinical/fork's from_timestamp).
  snapshot_timestamps: number[];
  nurse_notes: string;
}

// CCPC: counterfactual forks =================================================

export interface ForkTrajectoryPoint {
  timestamp: number; // unix seconds
  hr: number;
  rr: number;
  spo2: number;
  bp_sys: number;
  bp_dia: number;
  temp: number;
  lactate: number;
  crisis_type: string;
}

export interface PatientFork {
  fork_id: string;
  intervention_label: string;
  trajectory: ForkTrajectoryPoint[];
}

export interface ForkSummary {
  fork_id: string;
  forked_from_timestamp: number;
  created_at: number;
  intervention_label: string;
  trajectory_length: number;
}

export type ForkInterventionType =
  | 'administer_fluids'
  | 'administer_oxygen'
  | 'start_vasopressor'
  | 'administer_antibiotics'
  | 'administer_antipyretic';

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
    description: string;
    objectives: string[];
    // The exact action ids ScenarioRuntime::evaluate_action_correctness
    // (src/clinical/training_scenario.cpp) actually scores for this
    // scenario -- TrainingMode.ts must render buttons from this list, not
    // a fixed generic set, or clicks won't correspond to anything the
    // backend evaluates (see that file for the history of why).
    available_actions: { id: string; label: string }[];
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

export interface TrainingNoteDraft {
  session_id: string;
  scenario_id: string;
  draft_content: string;
  generated_at: string;
}

// INSTRUCTOR / COHORTS (mass education adoption) ============================
// Matches CohortManager (include/cohort_manager.h) and the
// handle_instructor_* handlers in src/server/http_server.cpp.

export interface Cohort {
  cohort_id: string;
  name: string;
  created_at: number;
  student_count: number;
}

export interface ImportedCredential {
  staff_id: string;
  name: string;
  password: string; // Plaintext, returned exactly once by the roster-import endpoint.
}

export interface CohortStudentSession {
  session_id: string;
  scenario_id: string;
  outcome: string;
  score: number;
  duration_seconds: number;
  missed_critical_windows: number;
}

export interface CohortStudentStats {
  staff_id: string;
  name: string;
  external_id: string;
  session_count: number;
  avg_score: number;
  sessions: CohortStudentSession[];
}

export interface MissedIntervention {
  scenario_id: string;
  failure: string;
  count: number;
}

export interface CohortDashboard {
  cohort: { cohort_id: string; name: string; created_at: number };
  students: CohortStudentStats[];
  top_missed_interventions: MissedIntervention[];
}

// UI & STATE ==================================================================

export interface AppState {
  // Auth
  signedIn: boolean;
  currentRole: Role | null;
  currentStaffName: string;
  selectedRole?: Role;

  // ACMK-OT session (see /api/acmk/session/init)
  acmkSessionId: string | null;
  acmkMode: 'simulation' | 'real_world' | null;

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
  // Set by endTraining() so the /debrief route (main-refactored.ts) has
  // something to render after navigating away from the training screen.
  lastTrainingReport: TrainingReport | null;
  
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

// ACMK-OT PLANES & STATE =====================================================

export interface StateFrame {
  session_id: string;
  timestamp: Date;
  cognitive_state: CognitiveState;
  risk_posture: RiskPosture;
  input_modalities: string[];
  confidence_global: number;
  patient_id?: string;
}

export interface SessionDescriptor {
  session_id: string;
  model_version: string;
  rule_set_version: string;
  clock_anchor: Date;
  permissions: Record<string, boolean>;
  device_fingerprint: string;
  user_role: Role;
}

// PERCEPTION LAYER ===========================================================

export interface PerceptualArtifact {
  artifact_id: string;
  // Backend-defined and open-ended (src/server/http_server.cpp currently
  // only ever emits "vitals_snapshot", the one real perceptual input this
  // system has -- vitals readings, not camera/audio frames) rather than a
  // fixed union invented ahead of what the system actually perceives.
  artifact_type: string;
  content_hash: string;
  timestamp: Date;
  confidence: number;
  alignment_metadata: string[];
}

// TRIGEMINAL PROCESSING LAYER ================================================

export interface InferenceNode {
  node_id: string;
  parent_nodes: string[];
  status: 'active' | 'suppressed' | 'converged';
  confidence: number;
  suppression_reason?: string;
  // {from, to} object, matching how ACMKAPIHandler::serialize_inference_node
  // (src/server/acmk_api_handler.cpp) actually serializes ACMK::InferenceNode's
  // std::pair<long,long> -- a bare 2-tuple here would silently misparse it.
  time_range: { from: number; to: number };
}

export interface InferenceGraph {
  nodes: InferenceNode[];
  // Derived client-side from nodes[].parent_nodes / .status rather than
  // fetched separately -- ACMK::InferenceNode has no dedicated edge or
  // suppression-marker storage of its own (src/kernel/acmk_planes.h), so
  // there's nothing for the server to serve at a separate endpoint.
  edges: Array<{ from: string; to: string; weight: number }>;
  suppression_markers: Array<{ node_id: string; reason: string }>;
}

// COGNITIVE DECISION LAYER ===================================================

export interface DecisionEnvelope {
  final_state: string;
  // Plain description strings (e.g. "SpO2 88% is below normal range"), not
  // a richer {constraint_id, name, weight} record -- this system doesn't
  // have a named/weighted constraint registry, just the clinical findings
  // that did or didn't end up driving the recommendation (see
  // ClinicalObservation in include/clinical_analyzer.h). A fabricated
  // weight would be decorative, not real.
  dominant_constraints: string[];
  rejected_alternatives: Array<{ id: string; reason: string }>;
  confidence_bounds: { low: number; high: number };
}

// TEMPORAL CONTROLS ==========================================================

export interface TemporalSnapshot {
  snapshot_id: string;
  session_id: string;
  timestamp: Date;
  state_hash: string;
}

export interface TemporalControlRequest {
  action: 'step' | 'pause' | 'resume' | 'rollback' | 'replay';
  from_timestamp?: Date;
  to_timestamp?: Date;
  speed?: number;
  reason?: string;
}

// HUMAN-IN-THE-LOOP ==========================================================

// Mirrors ACMK::HumanInterventionEvent (src/kernel/acmk_planes.h) as served
// by GET /api/acmk/environment/human-events -- there's one real event
// record type server-side, not a separate richer "Annotation" shape with
// its own id/immutability flag layered on top of it.
export interface HumanAction {
  event_type: 'acknowledge' | 'annotate' | 'flag_anomaly' | 'escalate' | 'alert_override';
  user_id: string;
  session_id: string;
  timestamp: Date;
  scope: string;
  content: string;
}

// SIMULATION =================================================================

export interface SimulationStatus {
  is_simulation: boolean;
  scenario_id?: string;
  session_id: string;
  is_active: boolean;
  elapsed_ms: number;
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
