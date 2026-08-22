import { Component } from './component';
import { apiClient } from '@api/client';
import { ExplainabilityPanelComponent } from './ExplainabilityPanel';
import { PerceptionLayerComponent } from './PerceptionLayer';
import { TrigeminalProcessingLayerComponent } from './TrigeminalLayer';
import { TemporalControlsComponent } from './TemporalControls';
import { HumanInTheLoopComponent } from './HumanInTheLoop';
import type { RoleCapabilities } from '@api/types';

export interface ReasoningTraceConfig {
  sessionId: string;
  roleCapabilities: RoleCapabilities;
  onBack: () => void;
}

// Session-level view of everything ACMK-OT recorded while reasoning this
// shift: what it perceived (PerceptionLayer), what reasoning nodes it
// produced (TrigeminalLayer), what it decided and why (ExplainabilityPanel),
// snapshots you can replay from (TemporalControls), and the human actions
// taken on it (HumanInTheLoop). All five previously existed as orphaned,
// unstyled, disconnected components with no real backend producer; this is
// the first place any of them actually show real data (see the trace-plane
// fixes and handle_scaffold producer in src/server/http_server.cpp).
export class ReasoningTraceView extends Component {
  private config: ReasoningTraceConfig;
  private explainability: ExplainabilityPanelComponent | null = null;
  private perception: PerceptionLayerComponent | null = null;
  private trigeminal: TrigeminalProcessingLayerComponent | null = null;
  private temporal: TemporalControlsComponent | null = null;
  private humanInTheLoop: HumanInTheLoopComponent | null = null;

  constructor(config: ReasoningTraceConfig) {
    super();
    this.config = config;
  }

  render(): string {
    return `
      <div class="min-h-screen bg-slate-950 text-slate-100 p-6 space-y-6 animate-fade-in-up">
        <div class="max-w-5xl mx-auto space-y-6">
          <div class="flex items-center justify-between">
            <button id="btn-reasoning-back" class="inline-flex items-center gap-2 text-cyan-400 hover:text-cyan-300 text-sm font-medium border border-cyan-500/30 rounded-lg px-4 py-2 transition-colors">
              ← Dashboard
            </button>
            <div class="text-xs text-slate-500">
              Session <code>${this.config.sessionId.substring(0, 8)}…</code>
            </div>
          </div>

          <div>
            <h1 class="text-2xl font-bold text-white">Reasoning Trace</h1>
            <p class="text-sm text-slate-400 mt-1">Real ACMK-OT cognitive-plane data for this session -- not a demo. Generate an SBAR from any patient to populate it.</p>
          </div>

          <div id="rt-explainability"></div>
          <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
            <div id="rt-perception"></div>
            <div id="rt-trigeminal"></div>
          </div>
          <div id="rt-temporal"></div>
          <div id="rt-hitl"></div>
        </div>
      </div>
    `;
  }

  mount(container: HTMLElement): void {
    container.innerHTML = this.render();
    this.element = container;
    this.on('#btn-reasoning-back', 'click', () => this.config.onBack());
    this.loadAndMountPanels();
  }

  unmount(): void {
    this.temporal?.unmount();
    this.element = null;
  }

  private async loadAndMountPanels(): Promise<void> {
    const { sessionId, roleCapabilities } = this.config;

    const [envelope, artifacts, graph, snapshots, events] = await Promise.all([
      apiClient.getDecisionEnvelope(sessionId).catch(() => null),
      apiClient.getPerceptualArtifacts(sessionId).catch(() => []),
      apiClient.getInferenceGraph(sessionId).catch(() => ({ nodes: [], edges: [], suppression_markers: [] })),
      apiClient.getSnapshots(sessionId).catch(() => []),
      apiClient.getHumanEvents(sessionId).catch(() => [])
    ]);

    if (!this.element) return; // navigated away while loading

    this.explainability = new ExplainabilityPanelComponent({ envelope });
    this.explainability.mount(this.element.querySelector('#rt-explainability')!);

    this.perception = new PerceptionLayerComponent({ artifacts });
    this.perception.mount(this.element.querySelector('#rt-perception')!);

    this.trigeminal = new TrigeminalProcessingLayerComponent({ graph });
    this.trigeminal.mount(this.element.querySelector('#rt-trigeminal')!);

    this.temporal = new TemporalControlsComponent({ sessionId, snapshots });
    this.temporal.mount(this.element.querySelector('#rt-temporal')!);

    this.humanInTheLoop = new HumanInTheLoopComponent({
      sessionId,
      events,
      canAnnotate: roleCapabilities.canAddNotes,
      canEscalate: roleCapabilities.canAcceptRecommendations,
      canFlag: true
    });
    this.humanInTheLoop.mount(this.element.querySelector('#rt-hitl')!);
  }
}
