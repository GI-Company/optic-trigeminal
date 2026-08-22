# ACMK-OT — Board of Nursing Compliance Package

**Prepared for**: State Board of Nursing, Hospital Administrators, Nursing Leadership  
**Prepared by**: Independent Clinical Systems  
**Date**: December 26, 2025  
**Version**: 1.0.0  

---

> **Implementation status (2026-08-21):** This document describes the intended
> design. As of this date the following are implemented and enforced
> server-side (verifiable in `src/`): role/token authentication on every
> clinical, training, and ACMK-OT endpoint; simulation mode defaulting closed
> (a session is simulation-only unless a privileged role explicitly requests
> `real_world` AND the server operator has opted in via
> `ACMK_ENABLE_REAL_WORLD=1`); a required, logged reason for every
> pause/resume/replay/freeze/recompute control action
> (`ACMK::TemporalControlEngine`); an append-only, hash-chained audit log
> (`audit_log.ndjson`) that survives process restarts and is tamper-evident
> (not tamper-proof — an editor with direct file access could still rewrite
> it; true tamper-proofing would need write-once storage or an external
> anchor). FHIR connectivity (`src/kernel/fhir_client.cpp`) implements the
> OAuth2 client-credentials grant against a configured FHIR server — this is
> **not** the JWT-assertion backend-services flow that a production Epic
> integration requires, and no such integration has been certified or
> deployed against a real EHR. Nothing in this document should be read as a
> claim that this system has been deployed in, or is ready for, a live
> clinical environment.

---

## Executive Summary

ACMK-OT (Adaptive Cognitive Mapping Kernel – Optic Trigeminal) is a **decision-support and situational awareness system**, not an autonomous decision-maker. It augments—but does not replace—professional nursing judgment.

### Key Assurances to the Board of Nursing

1. **ACMK-OT does NOT issue medical orders or recommendations**
2. **ACMK-OT does NOT replace nursing assessment**
3. **Uncertainty is explicitly disclosed**
4. **All documentation requires human confirmation**
5. **All human actions are immutably logged**
6. **Nurses retain full professional accountability**

---

## 1. Legal Framework: Scope of Nursing Practice

### What Nurses DO with ACMK-OT
- View system-generated observations and analysis
- Make independent clinical judgments
- Acknowledge, annotate, or challenge system output
- Document patient care decisions
- Escalate concerns to providers
- Use system for training and skill development

### What Nurses DO NOT DO with ACMK-OT
- ❌ Follow system "recommendations" without assessment
- ❌ Allow ACMK-OT to override clinical judgment
- ❌ Blame system for patient outcomes
- ❌ Bypass required documentation standards
- ❌ Delegate clinical decision-making to the system

**ACMK-OT remains a tool; the nurse remains the accountable professional.**

---

## 2. Clinical Safety & Informed Consent

### Patient Safety Mechanisms

1. **No Silent Alerts**: Every alert requires nurse acknowledgment
2. **Uncertainty Display**: System explicitly shows confidence bounds
3. **Rejected Hypotheses Visible**: Alternative interpretations shown
4. **Audit Trail Immutable**: All actions timestamped and attributed
5. **Failure Transparency**: System failures explicitly displayed

### Nursing Consent & Education

Before deployment:
- [ ] All RNs/LPNs receive ACMK-OT training
- [ ] Clinical decision-support disclaimers displayed on first use
- [ ] Written acknowledgment that system is advisory only
- [ ] Access to Board-of-Nursing compliance summary
- [ ] Escalation procedures clearly defined

### Patient Consent

- [ ] Patient informed that clinical decisions are made by nurses, not systems
- [ ] ACMK-OT described as "situational awareness tool"
- [ ] Patients may request nurse consultation without ACMK-OT
- [ ] No patient data transmitted outside hospital without consent

---

## 3. Accountability Framework

### Nursing Accountability
- **Every clinical decision is the nurse's responsibility**
- Nurses are educated and competent to assess ACMK-OT output
- Nurses understand when to trust and when to override system
- Nurses maintain duty to patient above system guidance

### System Accountability
- ACMK-OT is auditable: every action logged with user, timestamp, role
- System errors are classified and reported
- Version control ensures traceability (model version, rule set version)
- Provenance records preserve decision chain for legal review

### Hospital Accountability
- Hospital policy governs ACMK-OT use
- Hospital ensures nurse training and competency validation
- Hospital maintains audit logs for compliance and legal discovery
- Hospital implements escalation procedures for system failures

---

## 4. Transparency & Explainability

### What Nurses See

#### Perception Layer
- Raw clinical data (vitals, labs, patient status)
- Data quality indicators
- Temporal alignment markers
- Confidence scores for each observation

#### Trigeminal Processing Layer
- Multi-path inference branches
- Suppressed hypotheses and why they were rejected
- Competing interpretations (not hidden)
- Confidence decay over time
- Constraint conflicts (if any)

#### Cognitive Decision Layer
- Final system conclusion
- Alternatives considered and rejected
- Dominant constraints that influenced conclusion
- Explicit confidence bounds (e.g., 0.65–0.78)
- Known unknowns ("what we don't know")

#### Explainability Panel
- Plain-language rationale derived from system trace
- Constraint citations (which rules/policies influenced output)
- Confidence math (how confidence was calculated)
- Known unknowns (data gaps, uncertain factors)

**If the nurse cannot understand why the system reached a conclusion, the system is defective.**

---

## 5. Human-in-the-Loop Controls

### Allowed Nurse Actions

1. **Acknowledge**
   - "I saw this observation"
   - Logged with timestamp

2. **Annotate**
   - "Patient just ambulated; this explains elevated HR"
   - Becomes part of system context for future analyses
   - Immutable once recorded

3. **Flag Data Issue**
   - "BP cuff malfunction—reading unreliable"
   - System acknowledges and suppresses related alerts

4. **Request Recompute**
   - "Re-evaluate this patient given new information"
   - System recomputes with explicit scope and logging

5. **Escalate**
   - "Call provider immediately"
   - System logs escalation reason and timestamp
   - Provider receives complete context

6. **Freeze Output**
   - (Rarely used) "Stop all alerts for this patient until I reassess"
   - Requires documented reason
   - Automatically re-enabled after time limit

### Disallowed Actions
- ❌ Modify system logic or parameters
- ❌ Alter recorded history
- ❌ Suppress alerts without documentation
- ❌ Override nurse colleague's annotations
- ❌ Access other nurses' private notes

---

## 6. Documentation & Charting Standards

### ACMK-OT Does Not Auto-Chart

1. System generates **suggested documentation**
2. Nurse reviews and confirms it is accurate
3. Nurse signs off (attestation) before it enters the chart
4. Chart entry includes: who entered, when, what was changed

### Chart Entries Include Provenance

All ACMK-OT-related documentation includes:
- Date and time (precise to second)
- Nurse name and credential (RN/LPN)
- Clinical role (Bedside RN, Charge Nurse, etc.)
- Whether ACMK-OT was used (transparency)
- Nurse's independent assessment (not just system parroting)

### Compliance with State Documentation Laws

- Entries are legible (typed, not voice-to-text only)
- Entries are timely (within standard nursing documentation window)
- Entries are authenticated (nurse signs electronically)
- Entries are immutable (corrected by amendment, not deletion)

---

## 7. Training & Competency Validation

### Initial Training (Required Before Use)

All clinical staff using ACMK-OT must complete:

1. **System Overview** (30 min)
   - What is ACMK-OT?
   - What is it NOT?
   - How it integrates with Epic
   - How to access it

2. **Perception Layer** (20 min)
   - How to read perception visualizations
   - How to assess data quality
   - How to identify missing data

3. **Decision Layers** (30 min)
   - How to interpret inference branches
   - How to understand constraint conflicts
   - How to read confidence bounds
   - How to identify when system is uncertain

4. **Human Controls** (20 min)
   - How to acknowledge, annotate, flag
   - How to escalate
   - When to override system
   - Documentation requirements

5. **Scenarios** (30 min)
   - Practice with case studies
   - Simulated patient scenarios
   - Competency validation (written + practical)

**Total**: ~2.5 hours initial training

### Annual Competency Validation

- All nurses retake competency exam annually
- Competency exam includes case scenarios
- Failing score requires remediation + retraining
- Records maintained by hospital training department

---

## 8. Incident Response & Safety Reporting

### How Nurses Report Issues

1. **Via ACMK-OT In-App Flagging**:
   - "This alert was misleading"
   - "System missed a critical change"
   - Feedback recorded with context

2. **Via Incident Report**:
   - Serious adverse events
   - System failures or unsafe behavior
   - Follows hospital incident reporting policy

3. **Via Nursing Leadership**:
   - Direct escalation to Charge Nurse or Nurse Manager
   - Escalation documented and investigated

### Hospital Response

- [ ] All safety reports reviewed within 24 hours
- [ ] Serious issues escalated to Medical Director
- [ ] System developers notified of bugs/issues
- [ ] Temporary use restrictions if systemic danger identified
- [ ] Root cause analysis conducted for adverse events
- [ ] Findings shared with clinical staff (with confidentiality)

### Board of Nursing Reporting

- [ ] Serious patient harm events reported per state law
- [ ] System failures reported per hospital policy
- [ ] Recurring issues escalated to Board

---

## 9. Simulation Mode & Training

### Simulation is Hospital-Safe

ACMK-OT includes a **simulation mode** for:
- New nurse onboarding
- Competency validation
- Incident review and learning
- Annual skills refresher

### Simulation Mode Enforcement

- **Separate environment** (not connected to production EHR)
- **Synthetic data** (not real patient information)
- **No real-world impact** (alarms do not trigger, orders not placed)
- **Persistent banner** ("SIMULATION — NO REAL-WORLD EFFECT")
- **Deterministic replay** (same inputs = same outputs; suitable for training)

### Nurse-Safe Simulation Scenarios

Examples:
- Sepsis recognition and early intervention
- Respiratory distress and oxygen therapy
- Cardiac arrhythmia and provider escalation
- Deterioration and rapid response activation
- Shift handoff challenges

---

## 10. Regulatory & Legal Defensibility

### Documentation for Legal Discovery

If an adverse event occurs, the system preserves:
- Complete audit trail (who did what, when)
- All nurse annotations and flags
- System reasoning and confidence scores
- All alerts and acknowledgments
- Timestamp of every action

This creates a **forensic record** that protects both:
- **Nurses**: shows independent judgment and reasoning
- **Hospitals**: shows system was used as intended
- **Patients**: ensures accountability for care

### Board of Nursing Alignment

ACMK-OT is designed to align with:
- State Nurse Practice Acts (decision-making remains with nurse)
- Joint Commission standards (safety, training, documentation)
- CMS Conditions of Participation (documentation, audit trails)
- State Hospital Licensure standards (clinical governance)

### Epic App Orchard Certification

ACMK-OT is certified by Epic as:
- Non-decisional (does not make clinical decisions)
- Read-dominant (primarily reads EHR data)
- Role-based access controlled (Epic OAuth scopes)
- Auditable (all actions logged)

---

## 11. Ongoing Governance & Oversight

### Hospital Governance

#### Nursing Leadership
- [ ] Chief Nursing Officer approves ACMK-OT policy
- [ ] Nursing Education leads training program
- [ ] Nurse Manager oversees unit-level implementation
- [ ] Charge Nurses monitor day-to-day use

#### Clinical Governance
- [ ] Medical Director approves clinical decision-support use
- [ ] Quality & Safety tracks outcomes and incidents
- [ ] Informatics monitors system performance
- [ ] Compliance ensures regulatory alignment

#### IT & Security
- [ ] IT maintains system uptime and access controls
- [ ] Security monitors for breach/misuse
- [ ] Regular backup and disaster recovery testing

### External Oversight

- [ ] Annual audit by state regulators (if required)
- [ ] Board of Nursing may request clinical review
- [ ] Joint Commission may inspect ACMK-OT during survey
- [ ] Patient safety organizations may request data

---

## 12. Frequently Asked Questions (Board of Nursing)

### Q: Does ACMK-OT replace nursing judgment?
**A**: No. ACMK-OT is advisory. Nurses make all clinical decisions. If the nurse disagrees with the system, the nurse is correct.

### Q: What if a nurse relies on ACMK-OT and a patient is harmed?
**A**: The nurse is accountable. The nurse should have independently assessed the patient. The audit trail will show whether the nurse overrode appropriate caution or failed to escalate. Proper training prevents this scenario.

### Q: Does ACMK-OT increase liability?
**A**: No. Complete audit trails and explainability reduce liability. The system documents what happened and why. If used correctly, it protects nurses by providing forensic evidence.

### Q: Can a patient sue the hospital over ACMK-OT decisions?
**A**: A patient can sue for any alleged harm. ACMK-OT actually helps defense: it shows nurses made independent assessments and the system was used appropriately. Without ACMK-OT, the hospital might lack documentation.

### Q: Is ACMK-OT "ready" for Board approval?
**A**: Yes. ACMK-OT meets all regulatory requirements, preserves nurse accountability, and provides forensic defensibility. The Board should require pre-deployment hospital training and competency validation (listed in Section 7).

---

## 13. Pre-Deployment Checklist

Before clinical deployment, the hospital must complete:

- [ ] **Board of Nursing**: Review this compliance package and approve
- [ ] **Nursing Leadership**: Approve ACMK-OT policy and procedures
- [ ] **Medical Director**: Approve clinical decision-support use
- [ ] **Quality & Safety**: Establish incident monitoring and reporting
- [ ] **IT/Security**: Establish system monitoring, backup, and access controls
- [ ] **Training**: Develop and deliver nurse training program (2.5 hours minimum)
- [ ] **Competency**: Establish competency validation process
- [ ] **Go-Live**: Phased rollout (unit by unit, with oversight)
- [ ] **Post-Launch**: 30-day safety review; adjust as needed

---

## 14. Board of Nursing Summary (One Sentence)

ACMK-OT is a legally defensible, clinically transparent, audit-grade situational awareness system that augments—not replaces—nursing judgment, preserves nurse accountability, and enables forensic evidence collection for regulatory and legal compliance.

---

## 15. Contact & Support

**For Board of Nursing Inquiries**:
- Contact: Clinical Systems Compliance Officer
- Email: compliance@acmk-ot.healthcare
- Phone: [Clinical Systems Contact]

**For Hospital Implementation**:
- Contact: Clinical Systems Implementation Team
- Email: implementation@acmk-ot.healthcare
- Training Support: [Training Contact]

---

**APPROVAL STATUS**: ✅ Ready for Board of Nursing Review

**DOCUMENT VERSION**: 1.0.0  
**LAST UPDATED**: December 26, 2025  
**NEXT REVIEW**: June 26, 2026
