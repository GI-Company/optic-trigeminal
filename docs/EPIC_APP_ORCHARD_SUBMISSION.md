# ACMK-OT — Epic App Orchard Submission Packet

## Nurse-First Forensic Cognitive Interface

---

> **Implementation status (2026-08-21):** This packet describes the intended
> design; it has not been submitted to or certified by Epic. Server-side
> auth, closed-by-default simulation mode, reason-required control actions,
> and a hash-chained persistent audit log are implemented — see
> `docs/BOARD_OF_NURSING_COMPLIANCE.md`'s status note for specifics. The FHIR
> client (`src/kernel/fhir_client.cpp`) speaks standard OAuth2
> client-credentials, not Epic's required JWT-assertion backend-services
> flow (RS384-signed client assertion + registered JWKS) — that flow needs
> an RSA implementation this project doesn't have, and is the next step
> before any real Epic sandbox connection, let alone submission.

---

## 1. Application Overview (Epic Required)

### Application Identity
- **Application Name**: ACMK-OT (Adaptive Cognitive Mapping Kernel – Optic Trigeminal)
- **Vendor / Developer**: Independent Clinical Systems
- **Contact**: [Clinical Systems Contact Information]
- **Support URL**: https://acmk-ot.healthcare/support

### Application Type
- **Epic App Type**: SMART on FHIR – Contextual (In-Workflow)
- **Clinical Category**: Clinical Decision Support (Non-Interruptive, Observational)
- **Decision Authority**: None (Advisory / Observational)

### Primary Users
- Registered Nurses (RN)
- Licensed Practical Nurses (LPN)
- Charge Nurses

### Secondary Users
- Attending Providers (MD / DO / NP / PA)
- Residents / Fellows
- Rapid Response / Code Teams
- Quality & Safety Personnel
- Compliance / Risk Management

---

## 2. Clinical Purpose Statement (Epic Language)

ACMK-OT is a nurse-focused cognitive transparency system that renders clinical data, temporal patterns, and constraint interactions into an explainable, auditable visualization. The application does not generate diagnoses, orders, or autonomous clinical decisions. It exists to support human cognition, documentation clarity, and post-event review.

### What ACMK-OT Does
- **Visualizes** EHR-derived data in temporal and cognitive layers
- **Displays** uncertainty, competing hypotheses, and constraint conflicts
- **Allows** nurses to annotate, flag, and acknowledge system output
- **Preserves** provenance and auditability of all interactions
- **Supports** deterministic replay for training and legal defensibility

### What ACMK-OT Does NOT Do
- ❌ Place orders
- ❌ Modify medications
- ❌ Generate diagnoses
- ❌ Suppress uncertainty
- ❌ Auto-correct clinical reasoning
- ❌ Make autonomous decisions

---

## 3. Scope of Authority (Explicit)

### ACMK-OT SHALL:
1. Visualize EHR-derived data in temporal and cognitive layers
2. Display uncertainty, competing hypotheses, and constraint conflicts
3. Allow nurses to annotate, flag, and acknowledge system output
4. Preserve provenance and auditability of all interactions
5. Support deterministic replay for training and review
6. Restrict real-world data hooks during simulation mode
7. Display explicit simulation mode banner at all times
8. Preserve all audit trails for legal/regulatory review

### ACMK-OT SHALL NOT:
1. Place orders
2. Modify medications
3. Generate diagnoses
4. Suppress uncertainty
5. Auto-correct clinical reasoning
6. Override nurse assessment
7. Hide rejected hypotheses
8. Auto-document without human confirmation

---

## 4. SMART on FHIR Launch Contexts

### Required Launch Contexts
- `launch/patient` — Patient-in-context launch
- `launch/encounter` — Encounter-in-context launch
- `launch/practitioner` — Practitioner identity
- `launch/role` — Clinical role for permissions
- `launch/location` — Unit/facility location for unit-level views

### Behavior in Epic
- Launch context MUST be present for production launch
- Missing context forces read-only degraded mode
- Application explicitly displays context awareness status

---

## 5. OAuth Scope Declaration (Minimal & Defensible)

### Bedside Nurse (RN / LPN)

#### Read Scopes
- `patient/Patient.read` — Identity & demographics
- `patient/Encounter.read` — Care context
- `patient/Observation.read` — Vitals, labs, trends
- `patient/Condition.read` — Known problems
- `patient/AllergyIntolerance.read` — Safety awareness
- `patient/CarePlan.read` — Nursing context
- `patient/Flag.read` — Safety & concern indicators
- `patient/DocumentReference.read` — Notes & reports

#### Write Scopes
- `patient/DocumentReference.write` — Nursing notes, ACMK-OT annotations
- `patient/Flag.write` — Safety concerns, data integrity issues
- `patient/Observation.write` — **Human-entered nursing observations only** (NOT derived data)

#### Explicitly Excluded
- All order-related write scopes (`MedicationRequest.*`, `ServiceRequest.*`, `Procedure.*`)
- `Condition.write` (no diagnosis writing)

### Charge Nurse

#### Read Scopes
Same as Bedside Nurse + unit-scoped `Encounter.read`

#### Write Scopes
- `patient/DocumentReference.write` — Handoff notes, unit-level documentation
- `patient/Flag.write` — Unit safety flags

#### Epic Guardrail
Charge nurses MUST NOT receive provider scopes, even if licensed as providers.

### Attending Provider (MD / DO / NP / PA)

#### Read Scopes
- `patient/*.read` — Full clinical context (Imaging, Labs, Notes, Orders)

#### Write Scopes
- **`patient/DocumentReference.write` ONLY** — No orders via ACMK-OT

#### Epic Enforcement Rule
**ACMK-OT UI MUST NOT expose ordering UI, even if the OAuth token technically allows it.**

The ACMK-OT interface explicitly restricts writes to `DocumentReference` only, via role-based UI controls.

### Resident / Fellow

#### Read Scopes
Same as Attending Provider

#### Write Scopes
- `patient/DocumentReference.write` — Training notes
- `patient/Flag.write` — Questions / concerns

#### Supervision Requirement
Provenance records MUST attribute all writes to both resident and supervising provider.

### Rapid Response / Code Team

#### Read Scopes
- `patient/Observation.read` — Real-time vitals
- `patient/Encounter.read` — Event context
- `patient/Condition.read` — Problem list
- `patient/Flag.read` — Safety flags

#### Write Scopes
- `patient/DocumentReference.write` — Event documentation
- `patient/Flag.write` — Code event flags

#### Epic Safety Rule
Writes MUST be time-bound to the event encounter. Retrospective charting is disabled.

### Quality / Safety / Risk Management

#### Read Scopes
- `patient/*.read` — Historical versions for RCA
- `AuditEvent.read` — Audit trail access
- `Provenance.read` — Traceability

#### Write Scopes
- `patient/DocumentReference.write` — RCA documents only

#### Epic Limitation
No live-chart mutation permitted. Read-only access to production data.

### Legal / Compliance

#### Read Scopes (Read Only)
- `patient/DocumentReference.read`
- `AuditEvent.read`
- `Provenance.read`

#### Write Scopes
None.

### Education / Simulation (Epic Sandbox Only)

#### Environment
**Sandbox / SUP ONLY** — NO production Epic environment access.

#### Scopes
- `patient/*.read`
- `patient/DocumentReference.write`

#### Hard Rule
Simulation credentials use a separate Epic client ID / sandbox tenant.

---

## 6. Data Flow Summary (Epic Review Critical)

1. **Launch**: Epic launches ACMK-OT in patient or encounter context
2. **OAuth**: ACMK-OT requests scoped FHIR access token
3. **Data Ingestion**: ACMK-OT reads EHR data (read-dominant)
4. **Cognitive Processing**: Data is ingested into reasoning core (read-only to EHR)
5. **Front-End Rendering**:
   - Perception layer (what the system "saw")
   - Trigeminal inference layer (multi-path reasoning branches)
   - Cognitive decision layer (alternatives, constraints, uncertainty)
6. **Human Interaction**:
   - Nurses acknowledge, annotate, flag anomalies
   - Actions are timestamped and attributed
7. **Documentation Write-Back**:
   - Annotations → `DocumentReference` or `Flag`
   - All writes include Provenance (user, role, timestamp, version)
8. **Audit Trail**: Every human action immutably logged

**At no point does ACMK-OT mutate Epic data silently or bypass nurse confirmation.**

---

## 7. Provenance & Audit (Epic Mandatory)

Every write to the EHR includes immutable Provenance:

```json
{
  "Provenance": {
    "agent": [
      {
        "role": [{ "coding": [{ "code": "nursing" }] }],
        "who": { "reference": "Practitioner/{epic_user_id}" }
      }
    ],
    "recorded": "2025-12-26T14:35:00Z",
    "activity": { "coding": [{ "code": "ACMK-OT-ANNOTATION" }] },
    "entity": [
      {
        "what": { "reference": "Observation/{observation_id}" }
      }
    ],
    "extension": [
      {
        "url": "acmk-version",
        "valueString": "acmk-ot-v1.0"
      },
      {
        "url": "ruleset-version",
        "valueString": "rules-v1.0"
      }
    ]
  }
}
```

**Epic reviewers will validate this structure.**

---

## 8. Nursing Workflow Alignment

### Typical Bedside Nurse Use Case

1. **Open patient chart** in Epic
2. **Launch ACMK-OT** from sidebar or problem list
3. **Review**:
   - What data the system perceived
   - Where uncertainty exists
   - How conclusions converged over time
4. **Act**:
   - Acknowledge or annotate observation
   - Flag data quality issues
   - Escalate to provider if needed
5. **Document**: Return to Epic; ACMK-OT annotations already available as notes
6. **Handoff**: ACMK-OT context carries to next shift

**ACMK-OT does not delay care or require task switching.**

---

## 9. Simulation & Training Mode

### Environment
Epic Sandbox / SUP only (separate client ID, separate tenant)

### Capabilities
- Synthetic data injection (vitals, labs, conditions)
- Deterministic replay (same inputs → same outputs)
- Scenario libraries (sepsis, deterioration, handoff, etc.)
- Performance metrics and learner feedback

### UI Requirement
**Persistent banner at ALL times:**
```
[SIMULATION — NO REAL-WORLD EFFECT]
```

Simulation mode is enforced entirely by the back end:
- Actuators disabled
- All outputs tagged non-operative
- No write-back to production EHR
- Deterministic time advancement

---

## 10. Downtime & Failure Handling

### If Epic FHIR is Unavailable
1. ACMK-OT enters **read-only degraded mode**
2. Displays: **"EHR context unavailable"**
3. Does NOT replay cached data as current
4. Logs downtime state for audit
5. **Silent failure is disallowed**

### Graceful Degradation
- UI remains responsive
- Cached session context preserved (labeled as stale)
- User explicitly notified of context loss
- Real-time alerts suppressed (re-enabled on restoration)

---

## 11. Security & Privacy

### Data Handling
- No data stored beyond session scope unless explicitly documented
- No secondary use of data
- HIPAA-aligned transport (TLS 1.2+) and encryption
- Role-based access enforced via Epic OAuth scopes

### Break-Glass Logging
- Break-glass access (if any) is logged with reason and audit trail
- Accessible to compliance and legal teams only
- Non-reversible (permanent audit record)

---

## 12. Required Epic Disclaimers (Verbatim)

These statements MUST appear:

1. **In-App Help**:
   > "This application does not place clinical orders."
   > "This application does not replace clinical judgment."
   > "Uncertainty and competing interpretations are explicitly displayed."
   > "All documentation requires human confirmation."

2. **First-Launch Modal**:
   > "ACMK-OT is a decision-support tool. You remain accountable for all clinical decisions. When in doubt, consult your clinical supervisor."

3. **Session Banner** (if simulation):
   > "SIMULATION — NO REAL-WORLD EFFECT"

---

## 13. Epic Review Checklist

- ✅ **OAuth Scopes**: Minimal, role-aligned, clinically defensible
- ✅ **Launch Contexts**: All required contexts supported
- ✅ **Write Safety**: Only `DocumentReference` and `Flag` writes permitted
- ✅ **Ordering Prevention**: UI explicitly prevents order entry
- ✅ **Provenance**: Every write includes immutable traceability
- ✅ **Simulation Isolation**: Separate tenant, enforced at back end
- ✅ **Error Handling**: Graceful degradation, explicit user notification
- ✅ **Audit Trail**: All human actions logged immutably
- ✅ **Role Enforcement**: Epic OAuth scopes matched to clinical roles
- ✅ **Disclaimers**: Legal language included in-app and on submission
- ✅ **Failure Transparency**: No silent failures; user always informed
- ✅ **Performance**: <500ms response for EHR queries; <5s session init

---

## 14. Procurement & Legal Language (One Paragraph)

ACMK-OT is a nurse-centered SMART on FHIR application that integrates read-dominantly with Epic, supports limited and auditable documentation writes aligned with nursing scope, preserves uncertainty and provenance, enforces strict role-based access controls matched to Epic OAuth scopes, and explicitly avoids clinical decision automation, order entry, or diagnosis generation. Every user action is immutably logged; every EHR write includes provider identity and version attribution; and the system maintains forensic integrity for clinical, legal, and regulatory defensibility. Simulation mode is enforced at the back end, preventing accidental real-world impact. ACMK-OT is suitable for Board-of-Nursing review, Joint Commission audit, and hospital legal discovery.

---

## 15. Implementation Roadmap

### Phase 1: Epic Integration (Months 1–2)
- [ ] Register SMART app with Epic
- [ ] Implement OAuth 2.0 / OIDC flow
- [ ] Test launch contexts in Epic sandbox
- [ ] Validate FHIR resource reads
- [ ] Implement role-based UI gating

### Phase 2: Nursing Workflow (Months 2–3)
- [ ] Design Nurse Landing Dashboard
- [ ] Implement Bedside Nurse view
- [ ] Deploy annotation and flag creation
- [ ] Test with RN validators
- [ ] Integrate with Epic charting (back-write)

### Phase 3: Compliance & Safety (Months 3–4)
- [ ] Implement break-glass logging
- [ ] Finalize Provenance recording
- [ ] Conduct security audit
- [ ] Prepare Epic App Orchard submission
- [ ] Board of Nursing pre-review

### Phase 4: Hospital Deployment (Month 5)
- [ ] Deploy to Epic Sandbox (staff training)
- [ ] Controlled pilot (5–10 nurses)
- [ ] Iterate on feedback
- [ ] Board of Nursing approval
- [ ] Production rollout

---

## 16. Support & Escalation

### In-App Support
- Help menu with disclaimers
- Contact email for questions
- Link to Board-of-Nursing compliance summary

### Epic Integration Support
- Dedicated Epic liaison for OAuth / FHIR issues
- Sandbox testing support
- Production go-live support

### Legal / Compliance
- Board of Nursing compliance package
- Hospital legal review materials
- CMS / regulatory alignment documentation

---

**SUBMISSION STATUS**: ✅ Ready for Epic App Orchard Review

**LAST UPDATED**: December 26, 2025

**VERSION**: 1.0.0
