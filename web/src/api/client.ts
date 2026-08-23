import type {
  InferenceRequest,
  InferenceResponse,
  SystemStatus,
  HealthCheckResponse,
  Patient,
  PatientObservation,
  SBARResponse,
  ClinicalAction,
  ClinicalActionResponse,
  TrainingScenario,
  TrainingSession,
  TrainingStatus,
  TrainingAction,
  TrainingActionResponse,
  TrainingReport,
  Role,
  HumanAction,
  InferenceGraph,
  InferenceNode,
  PerceptualArtifact,
  DecisionEnvelope,
  TemporalSnapshot,
  Cohort,
  ImportedCredential,
  CohortDashboard,
  TrainingNoteDraft
} from './types';
import { getWasmBridge } from './wasm-bridge';

const TOKEN_STORAGE_KEY = 'acmk_auth_token';

export interface SignInResult {
  token: string;
  userId: string;
  staffName: string;
  role: Role;
  expiresAt: number; // unix seconds
}

/**
 * HTTP Client for OpticTrigeminal API
 * Handles all communication with backend server
 */
export class ApiClient {
  private baseUrl: string;
  private timeout: number = 30000; // 30 second timeout
  private token: string | null = null;

  constructor(baseUrl: string = 'http://localhost:8080') {
    this.baseUrl = baseUrl;
    // Survive a page reload without forcing a re-sign-in, same as any
    // normal web app session token.
    this.token = sessionStorage.getItem(TOKEN_STORAGE_KEY);
  }

  getToken(): string | null {
    return this.token;
  }

  /**
   * Authenticate against the real server-side auth system (src/server/auth_manager.cpp).
   * The returned role is server-verified -- the client never gets to choose it.
   */
  async signIn(staffId: string, password: string): Promise<SignInResult> {
    const raw = await this.request<{
      token: string;
      user_id: string;
      staff_name: string;
      role: Role;
      expires_at: number;
    }>('POST', '/api/auth/sign-in', { staff_id: staffId, password }, { skipAuth: true });

    this.token = raw.token;
    sessionStorage.setItem(TOKEN_STORAGE_KEY, raw.token);

    return {
      token: raw.token,
      userId: raw.user_id,
      staffName: raw.staff_name,
      role: raw.role,
      expiresAt: raw.expires_at
    };
  }

  signOut(): void {
    this.token = null;
    sessionStorage.removeItem(TOKEN_STORAGE_KEY);
  }

  isSignedIn(): boolean {
    return this.token !== null;
  }

  /**
   * Make HTTP request with error handling
   */
  private async request<T>(
    method: 'GET' | 'POST' | 'PUT' | 'DELETE',
    endpoint: string,
    body?: any,
    opts?: { skipAuth?: boolean }
  ): Promise<T> {
    const url = `${this.baseUrl}${endpoint}`;
    const headers: Record<string, string> = {
      'Content-Type': 'application/json'
    };
    if (this.token && !opts?.skipAuth) {
      headers['Authorization'] = `Bearer ${this.token}`;
    }
    const options: RequestInit = { method, headers };

    if (body) {
      options.body = JSON.stringify(body);
    }

    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), this.timeout);

    try {
      const response = await fetch(url, {
        ...options,
        signal: controller.signal
      });

      if (response.status === 401) {
        // Token expired/invalid server-side -- drop it locally too so the
        // UI doesn't keep sending a dead token on every subsequent request.
        this.signOut();
      }

      if (!response.ok) {
        let message = `HTTP ${response.status}: ${response.statusText}`;
        try {
          const errBody = await response.json();
          if (errBody?.error) message = errBody.error;
        } catch {
          // response body wasn't JSON; keep the generic message
        }
        throw new Error(message);
      }

      return await response.json();
    } finally {
      clearTimeout(timeoutId);
    }
  }

  // =========================================================================
  // HEALTH & STATUS
  // =========================================================================

  async health(): Promise<HealthCheckResponse> {
    return this.request<HealthCheckResponse>('GET', '/health');
  }

  async getStatus(): Promise<SystemStatus> {
    return this.request<SystemStatus>('GET', '/api/inference/native/status');
  }

  // =========================================================================
  // INFERENCE API
  // =========================================================================

  async infer(request: InferenceRequest): Promise<InferenceResponse> {
    try {
      const bridge = getWasmBridge();
      if (bridge && bridge.isReady()) {
        try {
          // bridge.infer() takes the prompt directly (see wasm-bridge.ts) --
          // this used to JSON.stringify() the whole request object and pass
          // that AS the prompt, so the model reasoned over literal text like
          // '{"prompt":"...","max_tokens":128}' instead of the actual
          // question, and the returned object didn't match InferenceResponse
          // at all (action_id/cumulative_score aren't fields on that type).
          console.log(`[WASM Edge] Running inference in-browser: ${request.prompt}`);
          const result: any = await bridge.infer(request.prompt, request.max_tokens ?? 128);
          return {
            prompt: request.prompt,
            response: result.response ?? String(result),
            type: result.type ?? 'wasm_edge',
            timestamp: new Date().toISOString() as any,
            confidence: result.confidence ?? 0,
            related_concepts: result.related_concepts ?? []
          };
        } catch (error) {
          console.warn(`[WASM Edge] Inference failed, falling back to network:`, error);
        }
      }
    } catch (error) {
      console.warn('WASM bridge not available, falling back to network.');
    }

    return this.request<InferenceResponse>(
      'POST',
      '/api/inference/native/infer',
      request
    );
  }

  async learn(
    prompt: string,
    response: string,
    wasGood: boolean
  ): Promise<any> {
    return this.request('POST', '/api/inference/native/learn', {
      prompt,
      response,
      was_good: wasGood
    });
  }

  // =========================================================================
  // CLINICAL API
  // =========================================================================

  // The real, server-simulated patient roster (src/kernel or ClinicalSimulator
  // -- see handle_patients in http_server.cpp), filtered server-side to
  // whichever patients this user is assigned to. This used to not exist at
  // all -- the dashboard populated itself from web/src/store/mockData.ts,
  // fully fabricated names/rooms/vitals disconnected from the same `sim`
  // instance every other clinical endpoint reads from.
  async getPatients(): Promise<Patient[]> {
    const result = await this.request<{ patients: Patient[] }>('GET', '/api/clinical/patients');
    return result.patients || [];
  }

  async getPatientObservations(patientId: number): Promise<PatientObservation[]> {
    const response = await this.request<any>(
      'POST',
      '/api/clinical/observations',
      { patient_id: patientId }
    );
    return response.observations || [];
  }

  async generateScaffold(
    patientId: number,
    sessionId?: string,
    vitals?: any,
    findings?: string[]
  ): Promise<SBARResponse> {
    return this.request<SBARResponse>(
      'POST',
      '/api/clinical/scaffold',
      {
        patient_id: patientId,
        session_id: sessionId || '',
        vitals,
        findings
      }
    );
  }

  async logAction(action: ClinicalAction): Promise<ClinicalActionResponse> {
    return this.request<ClinicalActionResponse>(
      'POST',
      '/api/clinical/action',
      {
        patient_id: action.patient_id,
        action: action.action,
        passcode_validated: action.passcode_validated,
        nurse_id: action.nurse_id,
        nurse_name: action.nurse_name
      }
    );
  }

  // Real server-side chart storage (POST /api/clinical/chart, persisted to
  // chart_log.ndjson) -- "Add Note" used to only ever touch browser-local
  // state (store.addChartEntry), so a refresh silently lost every note.
  async addChartEntryRemote(patientId: number, type: string, content: string): Promise<any> {
    return this.request('POST', '/api/clinical/chart', {
      patient_id: patientId,
      type,
      content
    });
  }

  async getChartEntriesRemote(patientId: number): Promise<{ entry_id: string; type: string; content: string; nurse: string; timestamp: number }[]> {
    const result = await this.request<any>('GET', `/api/clinical/chart?patient_id=${patientId}`);
    return result.entries || [];
  }

  // =========================================================================
  // UNIT MANAGEMENT (Charge Nurse / Provider)
  // =========================================================================

  async admitPatient(name: string, mrn: string, room: string, diagnosis: string, acuityScore: number): Promise<{ status: string; patient_id: number }> {
    return this.request('POST', '/api/clinical/patients/admit', {
      name, mrn, room, diagnosis, acuity_score: acuityScore
    });
  }

  async dischargePatient(patientId: number, reason: string): Promise<{ status: string; patient_id: number }> {
    return this.request('POST', '/api/clinical/patients/discharge', { patient_id: patientId, reason });
  }

  async listStaff(): Promise<{ staff_id: string; name: string; role: Role; assigned_patients: number[] }[]> {
    const result = await this.request<any>('GET', '/api/staff/list');
    return result.staff || [];
  }

  async assignPatient(staffId: string, patientId: number, unassign: boolean = false): Promise<{ status: string }> {
    return this.request('POST', '/api/clinical/patients/assign', { staff_id: staffId, patient_id: patientId, unassign });
  }

  // Alert overrides have no persisted alert-ID to reference (deriveAlerts()
  // in ClinicalDashboard.ts computes alerts live from vitals each render) --
  // this records the override as an audited human-in-the-loop event instead.
  async overrideAlert(patientId: number, alertDescription: string, reason: string, sessionId?: string): Promise<{ status: string; overridden_by: string }> {
    return this.request('POST', '/api/clinical/alerts/override', {
      patient_id: patientId,
      alert_description: alertDescription,
      reason,
      session_id: sessionId || ''
    });
  }

  async createStaffAccount(staffId: string, name: string, role: Role): Promise<{ status: string; staff_id: string; role: Role; password: string }> {
    return this.request('POST', '/api/staff/create', { staff_id: staffId, name, role });
  }

  // =========================================================================
  // INSTRUCTOR / COHORTS (mass education adoption)
  // =========================================================================

  async createCohort(name: string): Promise<Cohort> {
    return this.request<Cohort>('POST', '/api/instructor/cohorts/create', { name });
  }

  async listCohorts(): Promise<Cohort[]> {
    const result = await this.request<{ cohorts: Cohort[] }>('GET', '/api/instructor/cohorts');
    return result.cohorts || [];
  }

  async importRoster(cohortId: string, students: { name: string; external_id: string }[]): Promise<ImportedCredential[]> {
    const result = await this.request<{ imported: number; credentials: ImportedCredential[] }>(
      'POST', '/api/instructor/cohorts/roster', { cohort_id: cohortId, students }
    );
    return result.credentials || [];
  }

  async getCohortDashboard(cohortId: string): Promise<CohortDashboard> {
    return this.request<CohortDashboard>('GET', `/api/instructor/cohort?cohort_id=${encodeURIComponent(cohortId)}`);
  }

  async removeStudent(cohortId: string, staffId: string): Promise<{ status: string }> {
    return this.request('POST', '/api/instructor/cohorts/remove-student', { cohort_id: cohortId, staff_id: staffId });
  }

  // =========================================================================
  // TRAINING API
  // =========================================================================

  async listTrainingScenarios(): Promise<TrainingScenario[]> {
    const response = await this.request<any>(
      'GET',
      '/api/training/list'
    );
    return response.scenarios || [];
  }

  async startTraining(
    scenarioId: string,
    difficulty?: string
  ): Promise<TrainingSession> {
    return this.request<TrainingSession>(
      'POST',
      '/api/training/start',
      {
        scenario_id: scenarioId,
        difficulty
      }
    );
  }

  async getTrainingStatus(): Promise<TrainingStatus> {
    return this.request<TrainingStatus>(
      'GET',
      '/api/training/status'
    );
  }

  async executeTrainingAction(action: TrainingAction): Promise<TrainingActionResponse> {
    // Training scenario state (ScenarioRuntime, correctness evaluation,
    // vitals mutation -- src/clinical/training_scenario.cpp) is not part of
    // the WASM build (see wasm/CMakeLists.txt's WASM_SOURCES: no
    // src/clinical/*), so there is no in-browser engine that actually knows
    // how to score a clinical action. This used to run the action text
    // through the general-purpose WASM text inference call instead, which
    // can't evaluate correctness or move vitals -- it always returned a
    // hardcoded cumulative_score of 0 dressed up as a "success". Training
    // actions always need the real scenario state on the server.
    return this.request<TrainingActionResponse>(
      'POST',
      '/api/training/action',
      action
    );
  }

  async advanceTrainingTick(deltaSeconds: number = 30): Promise<any> {
    // handle_training_tick (src/server/http_server.cpp) reads delta_seconds
    // from the query string, not a tick_ms body field -- sending the body
    // param meant this always silently fell back to the handler's
    // hardcoded 30s default regardless of what was requested.
    return this.request('POST', `/api/training/tick?delta_seconds=${deltaSeconds}`);
  }

  async endTraining(earlyTermination: boolean = false): Promise<TrainingReport> {
    return this.request<TrainingReport>(
      'POST',
      '/api/training/end',
      { early_termination: earlyTermination }
    );
  }

  async getTrainingAnalytics(period?: string, userId?: string): Promise<any> {
    const params = new URLSearchParams();
    if (period) params.append('period', period);
    if (userId) params.append('user', userId);
    
    const query = params.toString();
    return this.request('GET', `/api/training/analytics${query ? `?${query}` : ''}`);
  }

  // Same rich shape endTraining() returns live (transcript, learning_summary,
  // improvement_areas) -- reconstructed server-side from persisted event
  // data, not a bare metrics summary. Restricted server-side to the nurse
  // who ran the session, or an instructor/admin reviewing it.
  async getTrainingReport(sessionId: string): Promise<TrainingReport> {
    return this.request<TrainingReport>('GET', `/api/training/report?session_id=${encodeURIComponent(sessionId)}`);
  }

  // AI-drafted note for a completed session -- deterministic, built only
  // from real event data (see build_training_note_draft_json server-side).
  // Owner-only: this generates and persists a NOTE_DRAFTED audit event,
  // then the nurse edits the returned text and calls signTrainingNote with
  // whatever they end up submitting.
  async generateTrainingNoteDraft(sessionId: string): Promise<TrainingNoteDraft> {
    return this.request<TrainingNoteDraft>('GET', `/api/training/note/draft?session_id=${encodeURIComponent(sessionId)}`);
  }

  async signTrainingNote(sessionId: string, content: string, wasEdited: boolean): Promise<{ status: string; session_id: string; signed_at: string }> {
    return this.request('POST', '/api/training/note/sign', { session_id: sessionId, content, was_edited: wasEdited });
  }

  // =========================================================================
  // ACMK-OT (cognitive planes: session, control, human-in-the-loop, audit)
  // =========================================================================

  async initAcmkSession(mode: 'simulation' | 'real_world' = 'simulation'): Promise<{
    status: string;
    session_id: string;
    model_version: string;
    rule_set_version: string;
    user_role: string;
    mode: string;
    timestamp: number;
  }> {
    return this.request('POST', '/api/acmk/session/init', {
      device_fingerprint: navigator.userAgent,
      mode
    });
  }

  async recordHumanEvent(eventType: string, scope: string, content: string, sessionId?: string): Promise<any> {
    return this.request('POST', '/api/acmk/environment/human-event', {
      event_type: eventType,
      session_id: sessionId || '',
      scope,
      content
    });
  }

  /** ADMIN-only: tail of the hash-chained audit_log.ndjson (src/kernel/state_plane.cpp). */
  async getRecentAudit(limit: number = 50): Promise<{ status: string; count: number; entries: any[] }> {
    return this.request('GET', `/api/acmk/audit/recent?limit=${limit}`);
  }

  async getHumanEvents(sessionId: string): Promise<HumanAction[]> {
    const result = await this.request<any>('GET', `/api/acmk/environment/human-events?session_id=${sessionId}`);
    return result.events || [];
  }

  // === Reasoning trace (Explainability / Perception / Trigeminal layers) ===
  // Populated by generateScaffold() when it's given a session_id (see
  // handle_scaffold in src/server/http_server.cpp) -- this is the same real
  // clinical reasoning (ClinicalAnalyzer::analyze_patient) that produces the
  // SBAR text, just also recorded into the ACMK-OT cognitive planes so it
  // can be reviewed here instead of only appearing as prose in a chart note.

  async getInferenceGraph(sessionId: string): Promise<InferenceGraph> {
    const result = await this.request<any>('GET', `/api/acmk/trace/graph?session_id=${sessionId}`);
    const nodes: InferenceNode[] = result.nodes || [];
    return {
      nodes,
      edges: nodes.flatMap(n => n.parent_nodes.map(p => ({ from: p, to: n.node_id, weight: n.confidence }))),
      suppression_markers: nodes.filter(n => n.status === 'suppressed').map(n => ({ node_id: n.node_id, reason: n.suppression_reason || '' }))
    };
  }

  async getPerceptualArtifacts(sessionId: string): Promise<PerceptualArtifact[]> {
    const result = await this.request<any>('GET', `/api/acmk/trace/artifacts?session_id=${sessionId}`);
    return result.artifacts || [];
  }

  async getDecisionEnvelope(sessionId: string): Promise<DecisionEnvelope | null> {
    try {
      const result = await this.request<any>('GET', `/api/acmk/inference/decision/get?session_id=${sessionId}`);
      return result.envelope || null;
    } catch {
      return null; // no envelope recorded yet for this session (404) -- not an error the caller needs to handle specially
    }
  }

  async getSnapshots(sessionId: string): Promise<TemporalSnapshot[]> {
    const result = await this.request<any>('GET', `/api/acmk/trace/snapshots?session_id=${sessionId}`);
    return result.snapshots || [];
  }

  // === Temporal controls (pause/resume/freeze/replay/recompute) ===========
  // Every clinical role can use these in simulation mode (the only mode the
  // frontend ever initializes -- see initAcmkSession above); the server
  // additionally requires an elevated role if a session were ever real-world
  // (src/server/acmk_api_handler.cpp: validate_temporal_control).

  async controlPause(sessionId: string, reason: string): Promise<{ status: string }> {
    return this.request('POST', '/api/acmk/control/pause', { session_id: sessionId, reason });
  }

  async controlResume(sessionId: string, reason: string): Promise<{ status: string }> {
    return this.request('POST', '/api/acmk/control/resume', { session_id: sessionId, reason });
  }

  async controlFreeze(sessionId: string, reason: string): Promise<{ status: string }> {
    return this.request('POST', '/api/acmk/control/freeze', { session_id: sessionId, reason });
  }

  async controlReplay(sessionId: string, fromTimestamp: number, speed: number, reason: string): Promise<{ status: string }> {
    return this.request('POST', '/api/acmk/control/replay', { session_id: sessionId, from_timestamp: fromTimestamp, speed, reason });
  }

  // =========================================================================
  // UTILITY
  // =========================================================================

  /**
   * Test connection to backend
   */
  async testConnection(): Promise<boolean> {
    const originalTimeout = this.timeout;
    this.timeout = 2000; // Fast timeout for health check
    try {
      await this.health();
      return true;
    } catch {
      return false;
    } finally {
      this.timeout = originalTimeout;
    }
  }

  /**
   * Set custom base URL (useful for testing)
   */
  setBaseUrl(url: string): void {
    this.baseUrl = url;
  }

  /**
   * Set request timeout
   */
  setTimeout(ms: number): void {
    this.timeout = ms;
  }
}

/**
 * Global API client instance
 */
export const apiClient = new ApiClient();
