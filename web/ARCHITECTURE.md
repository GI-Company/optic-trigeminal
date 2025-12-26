# Frontend Architecture Diagram

## Application Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    OpticTrigeminal Web App                   │
│                     (index.html entry)                       │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│              main-refactored.ts (Orchestrator)              │
│  • Initialize app                                           │
│  • Setup routes                                             │
│  • Manage navigation                                        │
│  • Connect components to API/Store                          │
└──────────────────────┬──────────────────────────────────────┘
                       │
          ┌────────────┼────────────┐
          │            │            │
          ▼            ▼            ▼
    ┌──────────┐  ┌────────┐  ┌──────────┐
    │  Router  │  │ Store  │  │ API      │
    │  System  │  │        │  │ Client   │
    └──────────┘  └────────┘  └──────────┘
                       │
                       ▼
          ┌────────────────────────────┐
          │   Route Navigation         │
          ├────────────────────────────┤
          │ /signin → SignInScreen     │
          │ /dashboard → Dashboard    │
          │ /patient → PatientDetail  │
          │ /training → TrainingMode  │
          └────────────────────────────┘
```

## Component Hierarchy

```
Component (Abstract Base)
├── SignInScreen
│   └── handles: role selection, authentication
├── Dashboard
│   ├── renders: patient grid, training controls
│   └── manages: patient navigation
├── PatientDetail
│   ├── renders: vital signs, chart
│   └── handles: clinical documentation
├── TrainingMode
│   ├── renders: training UI, vital signs
│   └── handles: clinical actions
├── ScenarioSelector
│   ├── renders: scenario list
│   └── handles: scenario selection
└── ConnectionError
    └── renders: error UI, retry button
```

## State Flow

```
                    ┌─────────────────┐
                    │   AppState      │
                    │                 │
                    │ • Auth info     │
                    │ • Patients      │
                    │ • Chart entries │
                    │ • Training data │
                    │ • View mode     │
                    │ • Audit log     │
                    └────────┬────────┘
                             │
                    ┌────────┴────────┐
                    │                 │
                    ▼                 ▼
            ┌──────────────┐   ┌─────────────┐
            │ Subscribe    │   │ Update      │
            │ listeners    │   │ via methods │
            └──────────────┘   └─────────────┘
                    │                 │
                    └────────┬────────┘
                             │
                    ┌────────▼─────────┐
                    │ Notify listeners │
                    │ Re-render views  │
                    └──────────────────┘
```

## API Integration Pattern

```
Component
    │
    ├─ render() ──────────────────┬──────────► HTML String
    │                             │
    ├─ mount(container)           │
    │   │                         │
    │   └─► Insert into DOM ──────┘
    │   └─► setupEventListeners()
    │
    └─ Event Handler
        │
        ├─► Callback (config.onAction)
        │   │
        │   ├─► apiClient.method()
        │   │
        │   ├─► store.update()
        │   │
        │   └─► re-render()
        │
        └─► unmount() ◄── when changing routes
```

## Data Flow (Example: Sign In)

```
1. User clicks role button
   └─► SignInScreen.selectRole(role)

2. User clicks Sign In
   └─► config.onSignIn(role)
       └─► main-refactored.ts: store.signIn(role, displayName)

3. Store notifies listeners
   └─► store.subscribe() updates

4. Load patients
   └─► loadPatients() → apiClient (mock or real)
       └─► store.setPatients(patients)

5. Navigate
   └─► router.navigate('/dashboard')
       └─► renderDashboard()
           └─► new Dashboard({...config})
               └─► dashboard.mount(app)
```

## Event Handling Flow

```
┌────────────────────────────────────┐
│ Component.mount(container)         │
│                                    │
│ 1. Render HTML                     │
│ 2. Insert into DOM                 │
│ 3. Call setupEventListeners()      │
│    │                               │
│    ├─► this.on(selector, event...) │
│    │   │                           │
│    │   ├─► element.addEventListener│
│    │   │                           │
│    │   └─► Call handler            │
│    │                               │
│    └─► Store element reference     │
└────────────────────────────────────┘
```

## Lifecycle Management

### Component Lifecycle

```
Created
  │
  ├─► Constructor
  │   └─► Store config/props
  │
  └─► mount(container)
      ├─► Render HTML
      ├─► Insert into DOM
      ├─► Store element reference
      └─► Setup event listeners
          │
          └─► Ready for interaction


User Interaction
  │
  ├─► Event fires
  ├─► Handler called
  ├─► Side effects (API, state)
  ├─► Component re-renders (or refresh via callback)
  └─► UI updates


Navigation Away
  │
  └─► unmount()
      ├─► Remove element from DOM
      ├─► Clear event listeners
      ├─► Clean up references
      └─► Component destroyed
```

## Routing Logic

```
Router.navigate(path)
  │
  ├─► Lookup handler: routes.get(path)
  │   │
  │   └─► Found?
  │       ├─ Yes: execute handler()
  │       └─ No: warn + return
  │
  └─► Handler executes
      └─► Render appropriate component
          ├─► Create new component instance
          ├─► Mount to #app container
          └─► Previous component unmounted
```

## State Update Cycle

```
Action Triggered
  │
  └─► store.method(data)
      │
      ├─► Update internal state
      │
      ├─► Notify listeners
      │   │
      │   └─► subscriber(newState)
      │       │
      │       └─► Component re-renders
      │           └─► UI updates
      │
      └─► Audit logging (optional)
```

## Component Communication Pattern

```
Parent (main-refactored.ts)
  │
  ├─► Create component with config
  │   └─► Provide callbacks
  │
  ├─► Component.mount(container)
  │
  └─► Wait for user interaction
      │
      └─► User interaction
          │
          └─► Component calls callback
              │
              ├─► Parent receives signal
              │
              ├─► Parent updates state
              │
              ├─► Parent creates new component
              │
              └─► Navigate to new route
```

## Error Handling Flow

```
Initialize App
  │
  └─► apiClient.testConnection()
      │
      ├─ Healthy?
      │  ├─ Yes: continue init
      │  │       └─► Load scenarios
      │  │
      │  └─ No: show ConnectionError
      │        └─► User can retry
      │
      └─► Catch exceptions
          └─► showConnectionError()
              └─► Display error component
```

## UI Rendering Pattern

```
HTML String (Template)
  │
  ├─► Insert into container.innerHTML
  │   │
  │   └─► Browser parses & renders
  │
  ├─► Query elements: this.querySelector()
  │
  └─► Attach listeners: this.on()
      │
      └─► Ready for interaction
```

## Data Flow During Training

```
Dashboard
  │
  └─► User clicks "Start Training"
      │
      └─► ScenarioSelector mounts
          │
          └─► User selects scenario
              │
              └─► apiClient.startTraining(scenarioId)
                  │
                  └─► Get TrainingSession from backend
                      │
                      └─► store.startTraining(session)
                          │
                          └─► router.navigate('/training')
                              │
                              └─► TrainingMode mounts
                                  │
                                  ├─► Display patient vitals
                                  │
                                  └─► Ready for actions
                                      │
                                      └─► User clicks action button
                                          │
                                          └─► config.onAction()
                                              │
                                              └─► apiClient.executeTrainingAction()
                                                  │
                                                  └─► Update session score
                                                      │
                                                      └─► Re-render TrainingMode
```

## Type Safety Chain

```
API Response
  │
  ├─► Parse JSON
  │
  ├─► Cast to TypeScript type
  │   └─► e.g., InferenceResponse
  │
  ├─► Pass to Component
  │   └─► Component props typed
  │
  ├─► Store in State
  │   └─► State interface typed
  │
  └─► Render in Template
      └─► All operations type-checked
```

## Performance Characteristics

```
Load Time
  ├─► HTML loads
  ├─► main-refactored.ts executes
  ├─► API health check (fast)
  ├─► Mount SignInScreen (immediate)
  └─► Ready for user input (< 2 seconds)

Interaction Time
  ├─► Click event (0ms)
  ├─► Handler called (0ms)
  ├─► API call (50-500ms)
  ├─► State update (0ms)
  ├─► Component re-render (< 50ms)
  └─► DOM update visible (< 16ms/frame)

Memory Usage
  ├─► Component instances: ~1-2 active
  ├─► Event listeners: ~5-15 per component
  ├─► State store: ~1 singleton
  ├─► No external framework overhead
  └─► Minimal bundle size
```

## Scaling Considerations

### Adding New Features

1. **New Page/View**
   ```typescript
   // 1. Create component
   export class NewFeature extends Component { ... }
   
   // 2. Register route
   router.register('/new-feature', renderNewFeature);
   
   // 3. Add navigation
   router.navigate('/new-feature');
   ```

2. **New API Integration**
   ```typescript
   // Add method to ApiClient
   async getNewData(): Promise<NewDataType> { ... }
   
   // Use in component callback
   config.onFetch = async () => {
     const data = await apiClient.getNewData();
     // ...
   }
   ```

3. **New State Property**
   ```typescript
   // Update AppState interface
   interface AppState {
     // ... existing
     newProperty: string;
   }
   
   // Add manager method
   setNewProperty(value: string): void { ... }
   ```

### Extensibility Points

1. **Component Base Class** - Add shared functionality
2. **Router** - Add new routes easily
3. **Store** - Add new state properties
4. **API Client** - Add new endpoints
5. **Utilities** - Add helper functions

All designed for minimal impact on existing code.
