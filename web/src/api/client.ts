import type {
  InferenceRequest,
  InferenceResponse,
  SystemStatus,
  HealthCheckResponse,
  PatientObservation,
  SBARResponse,
  ClinicalAction,
  ClinicalActionResponse,
  TrainingScenario,
  TrainingSession,
  TrainingStatus,
  TrainingAction,
  TrainingActionResponse,
  TrainingReport
} from './types';

/**
 * HTTP Client for OpticTrigeminal API
 * Handles all communication with backend server
 */
export class ApiClient {
  private baseUrl: string;
  private timeout: number = 30000; // 30 second timeout

  constructor(baseUrl: string = 'http://localhost:8080') {
    this.baseUrl = baseUrl;
  }

  /**
   * Make HTTP request with error handling
   */
  private async request<T>(
    method: 'GET' | 'POST' | 'PUT' | 'DELETE',
    endpoint: string,
    body?: any
  ): Promise<T> {
    const url = `${this.baseUrl}${endpoint}`;
    const options: RequestInit = {
      method,
      headers: {
        'Content-Type': 'application/json'
      }
    };

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

      if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
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
    vitals?: any,
    findings?: string[]
  ): Promise<SBARResponse> {
    return this.request<SBARResponse>(
      'POST',
      '/api/clinical/scaffold',
      {
        patient_id: patientId,
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
    return this.request<TrainingActionResponse>(
      'POST',
      '/api/training/action',
      action
    );
  }

  async advanceTrainingTick(tickMs: number = 1000): Promise<any> {
    return this.request('POST', '/api/training/tick', { tick_ms: tickMs });
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

  async getTrainingReport(sessionId: string): Promise<any> {
    return this.request('GET', `/api/training/report?session_id=${sessionId}`);
  }

  // =========================================================================
  // UTILITY
  // =========================================================================

  /**
   * Test connection to backend
   */
  async testConnection(): Promise<boolean> {
    try {
      await this.health();
      return true;
    } catch {
      return false;
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
