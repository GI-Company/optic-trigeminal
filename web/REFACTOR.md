# Frontend Refactoring - Architecture Documentation

## Overview

The OpticTrigeminal web frontend has been refactored from a monolithic architecture to a **component-based architecture** with clear separation of concerns.

## Architecture

### Directory Structure

```
web/src/
├── components/          # Reusable UI components
│   ├── component.ts     # Base Component class
│   ├── SignInScreen.ts  # Authentication screen
│   ├── Dashboard.ts     # Main dashboard view
│   ├── PatientDetail.ts # Patient detail view
│   ├── TrainingMode.ts  # Training scenario view
│   ├── ScenarioSelector.ts  # Scenario selection modal
│   └── ConnectionError.ts   # Error display
├── api/
│   ├── client.ts        # HTTP client
│   ├── types.ts         # Type definitions
│   └── wasm-bridge.ts   # WASM integration
├── store/
│   └── state.ts         # Global state management
├── utils/
│   ├── router.ts        # Client-side routing
│   └── ui-helpers.ts    # UI utility functions
├── styles/
│   └── index.css        # Global styles
└── main-refactored.ts   # Application entry point
```

## Key Components

### 1. Base Component Class (`component.ts`)

Abstract base class for all UI components with common functionality:

```typescript
abstract class Component {
  protected element: HTMLElement | null = null;
  
  abstract render(): string;
  abstract mount(container: HTMLElement): void;
  abstract unmount(): void;
  
  protected querySelector<T extends HTMLElement>(selector: string): T | null;
  protected querySelectorAll<T extends HTMLElement>(selector: string): T[];
  protected on(selector: string, event: string, handler: (e: Event) => void): void;
  protected once(selector: string, event: string, handler: (e: Event) => void): void;
}
```

**Benefits:**
- Consistent API across components
- Built-in event delegation
- Automatic element lifecycle management

### 2. SignInScreen Component

Handles user authentication and role selection.

**Features:**
- Multi-role support (RN, Charge Nurse, Provider, Admin, IT)
- Visual feedback on role selection
- Role capability definitions

### 3. Dashboard Component

Main clinical dashboard with patient grid.

**Features:**
- Patient card grid with vital signs
- Training scenario launcher
- Auto-refresh capability
- System log integration

### 4. PatientDetail Component

Detailed patient view with clinical charting.

**Features:**
- Vital signs display
- Clinical documentation
- SBAR scaffold generation
- Chart entry management
- Role-based access control

### 5. TrainingMode Component

Clinical training scenario execution.

**Features:**
- Real-time vital signs display
- Clinical action buttons
- Session scoring
- Time tracking

### 6. ScenarioSelector Component

Modal for scenario selection.

**Features:**
- Scenario list with metadata
- Easy cancellation
- Responsive layout

### 7. ConnectionError Component

Error display when backend unavailable.

**Features:**
- Clear error message
- Retry mechanism
- Instructions for starting backend

## Routing System

Simple client-side router for navigation:

```typescript
router.register('/signin', renderSignIn);
router.register('/dashboard', renderDashboard);
router.register('/patient', renderPatientDetail);
router.register('/training', renderTrainingMode);

router.navigate('/dashboard');
```

**Benefits:**
- No external routing library needed
- Lightweight and predictable
- Easy to extend

## State Management

Centralized state management via `StateManager`:

```typescript
store.signIn(role, displayName);
store.selectPatient(patientId);
store.startTraining(session);
store.addChartEntry(entry);
```

**Features:**
- Immutable state updates
- Subscriber notification
- Audit logging
- State reset capability

## UI Utilities

Helper functions in `ui-helpers.ts`:

- `showToast()` - Display notifications
- `debounce()` - Debounce function calls
- `formatDuration()` - Format time values
- `formatDate()` - Format dates
- `getVitalColor()` - Color code vital signs
- `createLoadingSpinner()` - Loading UI

## Type Safety

Comprehensive TypeScript types in `api/types.ts`:

- `Patient`, `Vitals` - Patient data models
- `TrainingSession`, `TrainingScenario` - Training models
- `InferenceRequest`, `InferenceResponse` - API models
- `Role`, `RoleCapabilities` - Access control
- `ChartEntry`, `SBARScaffold` - Clinical documentation

## Event Handling

Components use event delegation via protected methods:

```typescript
this.on('#btn-submit', 'click', (e) => {
  // Handle click
});

this.once('#btn-once', 'click', (e) => {
  // Handle one-time click
});
```

## Migration from Old Code

### Before (Monolithic)

```typescript
// main.ts - 700+ lines
function renderDashboard() {
  app.innerHTML = `...` // HTML string
  document.getElementById('btn-signout')?.addEventListener('click', ...)
  // Mixed logic, rendering, and event handling
}
```

### After (Component-Based)

```typescript
// main-refactored.ts - Clean orchestration
function renderDashboard(): void {
  const dashboard = new Dashboard({
    currentStaffName: state.currentStaffName,
    patients: state.patients,
    onSignOut: () => router.navigate('/signin'),
    // ...
  });
  dashboard.mount(app);
}
```

**Benefits:**
- Separation of concerns
- Reusable components
- Easier testing
- Better maintainability
- Cleaner codebase

## Styling

Uses Tailwind CSS with custom utility classes:

```html
<div class="bg-slate-800 border border-cyan-500/30 rounded-lg p-6">
  <h2 class="text-xl font-bold text-cyan-300">Title</h2>
</div>
```

## Component Composition

Components are composed in `main-refactored.ts`:

1. **Authentication Flow**
   - SignInScreen → Dashboard
   
2. **Navigation Flow**
   - Dashboard → PatientDetail (on patient select)
   - Dashboard → ScenarioSelector → TrainingMode
   
3. **Error Handling**
   - ConnectionError (fallback on init fail)

## Testing Considerations

Components are designed for testability:

```typescript
// Easy to mock
const mockComponent = new Dashboard({
  patients: testData,
  onSignOut: jest.fn(),
  // ...
});
mockComponent.mount(document.body);
```

## Future Improvements

1. **State Persistence** - Save state to localStorage
2. **Animations** - Add transitions between views
3. **Responsive Design** - Improve mobile layout
4. **Accessibility** - Add ARIA labels
5. **Component Library** - Extract reusable UI components
6. **Form Validation** - Add input validation utilities
7. **Error Boundaries** - Add error catching components
8. **Performance** - Implement virtual scrolling for large lists

## Development Workflow

1. Create new component extending `Component` base class
2. Implement `render()`, `mount()`, `unmount()`
3. Add event listeners in private method
4. Register route in `main-refactored.ts`
5. Define TypeScript interfaces for props
6. Use Tailwind for styling

## API Integration

Components receive callbacks for API calls:

```typescript
const dashboard = new Dashboard({
  // ...
  onStartTraining: async (scenarioId) => {
    const session = await apiClient.startTraining(scenarioId);
    store.startTraining(session);
  }
});
```

This keeps components decoupled from API details.

## Dependencies

- TypeScript 5+
- Tailwind CSS (CDN loaded in HTML)
- Browser APIs (Fetch, DOM, Web APIs)
- No external UI frameworks

## Performance

- **No runtime compilation** - Pre-compiled TypeScript
- **Minimal bundle size** - No heavy dependencies
- **Efficient rendering** - Manual DOM updates
- **Optimized selectors** - Cached element queries

## Maintenance

- Each component in separate file
- Clear responsibility boundaries
- Consistent naming conventions
- Comprehensive JSDoc comments
- Type-safe throughout
