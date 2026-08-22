# Modern Web Frontend - OpticTrigeminal v3.0.0

**Status:** Phase 1 Complete (Architecture + Core Infrastructure)  
**Framework:** TypeScript + Vite (no heavy frameworks)  
**Bundle Tool:** Vite 5.0  
**Target:** ~50KB gzipped final bundle

---

## What Changed

### Before (Old System)
- Single `web/index.html` file (2,896 lines)
- Inline CSS (~1500 lines)
- Inline JavaScript (57 functions, all global scope)
- Mixed concerns (HTML, CSS, JS tangled together)
- Difficult to test, maintain, or extend

### After (New System)
```
web/
├── index.html                    # Minimal entry point (25 lines)
├── package.json                  # Dependencies
├── tsconfig.json                 # TypeScript config
├── vite.config.ts               # Build config
└── src/
    ├── main.ts                  # ~600 lines app logic
    ├── api/
    │   ├── client.ts           # HTTP client (~200 lines)
    │   └── types.ts            # Full type definitions (~400 lines)
    ├── store/
    │   └── state.ts            # State management (~300 lines)
    └── styles/
        └── index.css           # Global styles (~300 lines)
```

**Benefits:**
- ✅ Modular architecture
- ✅ Full TypeScript support
- ✅ Hot reload during development
- ✅ Tree-shaking & dead code elimination
- ✅ Automatic code splitting
- ✅ Source maps for debugging
- ✅ Modern ESM imports
- ✅ Zero runtime overhead

---

## Quick Start

### 1. Install Dependencies
```bash
cd web
npm install
```

### 2. Development Mode
```bash
npm run dev
```
Opens browser at `http://localhost:5173` with hot reload

### 3. Build for Production
```bash
npm run build
```
Outputs to `web/dist/` with optimized bundle

### 4. Preview Production Build
```bash
npm run preview
```

---

## Project Structure

### Core Files

#### `src/main.ts` - Application Entry Point
- Handles all UI rendering
- Manages application lifecycle
- Orchestrates state updates
- ~600 lines of clean, readable code

**Key Functions:**
```typescript
initializeApp()        // Load config, check backend
renderSignInScreen()   // Role selection UI
renderDashboard()      // Main patient grid view
renderPatientDetail()  // Patient charting view
renderTrainingMode()   // Training scenario UI
```

#### `src/api/types.ts` - Type Definitions
Complete TypeScript interfaces for all API responses and domain models:
- Patient, Vitals, Observation
- ChartEntry, SBARScaffold
- TrainingSession, TrainingScenario
- AppState, AuditEntry
- Validation functions

**Benefits:**
- IDE autocomplete
- Type checking at compile time
- Self-documenting code
- Prevents runtime errors

#### `src/api/client.ts` - HTTP Client
Unified API client with error handling:
- `apiClient.health()` - Check server
- `apiClient.infer()` - Inference
- `apiClient.getPatientObservations()` - Clinical data
- `apiClient.logAction()` - Chart actions
- `apiClient.startTraining()` - Begin scenario
- Auto-retry, timeouts, error handling

**Usage:**
```typescript
import { apiClient } from '@api/client';

const response = await apiClient.infer({
  prompt: "what is 2 + 2",
  max_tokens: 128
});
```

#### `src/store/state.ts` - State Management
Central store for all application state:
- Auth (role, staff name, sign-in status)
- Patient data
- Clinical chart entries
- Training sessions
- UI toggles

**API:**
```typescript
import { store } from '@store/state';

// Get current state
const state = store.getState();

// Update state
store.signIn('rn', 'Sarah Johnson');
store.addChartEntry(entry);

// Subscribe to changes
store.subscribe((newState) => {
  // React to state changes
});
```

#### `src/styles/index.css` - Styling
Global styles with CSS variables:
- Color scheme (dark theme)
- Typography
- Components (cards, alerts, badges)
- Animations (fade, slide, pulse)
- Responsive utilities
- Scrollbar styling

**CSS Variables:**
```css
--ot-bg: #0f172a              /* Main background */
--ot-accent: #38bdf8          /* Cyan accent */
--ot-warn: #fbbf24            /* Warning color */
--ot-critical: #ef4444        /* Critical/error */
```

---

## Architecture Patterns

### Component Pattern (Without Framework)
Each component is a function that renders HTML:

```typescript
function renderPatientCard(patient: Patient) {
  return `
    <div class="patient-card">
      <h3>${patient.name}</h3>
      <div>HR: ${patient.vitals.hr}</div>
    </div>
  `;
}

// Usage
document.getElementById('grid').innerHTML = 
  patients.map(renderPatientCard).join('');
```

### Event Delegation
```typescript
document.addEventListener('click', (e) => {
  const btn = (e.target as HTMLElement).closest('.role-button');
  if (btn) {
    const role = btn.getAttribute('data-role');
    handleRoleSelection(role);
  }
});
```

### State Subscription Pattern
```typescript
import { useAppState } from '@store/state';

// Subscribe to state changes
const unsubscribe = useAppState((state) => {
  if (state.selectedPatientId !== previousId) {
    renderPatientDetail();
  }
});

// Later: stop listening
unsubscribe();
```

### Error Handling
```typescript
try {
  const response = await apiClient.startTraining(scenarioId);
  store.startTraining(response);
  renderTrainingMode();
} catch (error) {
  showError(`Failed to start training: ${error}`);
}
```

---

## Development Workflow

### Adding a New Feature

1. **Define Types** (`src/api/types.ts`)
   ```typescript
   export interface NewFeature {
     id: string;
     name: string;
     status: 'active' | 'inactive';
   }
   ```

2. **Add API Method** (`src/api/client.ts`)
   ```typescript
   async newFeatureRequest(): Promise<NewFeature> {
     return this.request('POST', '/api/new-feature', {});
   }
   ```

3. **Update State** (`src/store/state.ts`)
   ```typescript
   setState({
     ...state,
     newFeatures: [...]
   });
   ```

4. **Render UI** (`src/main.ts`)
   ```typescript
   function renderNewFeature() {
     app.innerHTML = `
       <div>New feature HTML</div>
     `;
   }
   ```

### Testing During Development

1. **Type Checking**
   ```bash
   npm run type-check
   ```

2. **In-Browser Testing**
   - Open http://localhost:5173
   - Check browser console for errors
   - Network tab shows API calls
   - React to state changes

3. **Production Build Test**
   ```bash
   npm run build && npm run preview
   ```

---

## Building for Production

### Single Command Build
```bash
npm run build
```

This:
1. Type checks all TypeScript
2. Bundles with Vite
3. Minifies with Terser
4. Creates source maps
5. Outputs to `dist/`

### Output Structure
```
dist/
├── index.html              # Main page
├── css/
│   └── index-[hash].css   # Bundled CSS
├── js/
│   └── main-[hash].js     # Bundled JS
└── assets/
    └── [other files]
```

### Bundle Size Target
- **Goal:** < 50KB gzipped
- **Current:** ~35KB gzipped (without WASM)
- **Breakdown:**
  - CSS: ~8KB
  - JavaScript: ~27KB
  - HTML: <1KB

---

## Integration with Backend

### Backend Running
```bash
./build/optic-trigeminal
# Server on http://localhost:8080
```

### Frontend Development
```bash
cd web && npm run dev
# Dev server on http://localhost:5173
# Auto-proxies /api calls to http://localhost:8080
```

### CORS Configuration
Vite dev server proxies API calls automatically:
```javascript
// vite.config.ts
proxy: {
  '/api': {
    target: 'http://localhost:8080',
    changeOrigin: true
  }
}
```

---

## Deployment

### Docker Build
```dockerfile
FROM node:20-alpine as builder
WORKDIR /app
COPY web .
RUN npm install && npm run build

FROM nginx:alpine
COPY --from=builder /app/dist /usr/share/nginx/html
EXPOSE 80
CMD ["nginx", "-g", "daemon off;"]
```

### Static Hosting
- Copy `web/dist/` contents to any static host
- Works with Netlify, Vercel, AWS S3, etc.
- Backend must be accessible from client

### Environment Configuration
Create `.env.production`:
```
VITE_API_BASE=https://api.example.com
```

Access in code:
```typescript
const baseUrl = import.meta.env.VITE_API_BASE;
```

---

## Migrating from Old System

### Old Way (2,896-line HTML file)
```html
<script>
  function updatePatient() {
    // Direct DOM manipulation
    document.getElementById('hr-value').textContent = '85';
  }
</script>
```

### New Way (Modular TypeScript)
```typescript
// src/main.ts
async function loadAndRenderPatients() {
  const patients = await apiClient.getPatients();
  store.setPatients(patients);
  renderDashboard();
}
```

**Key Improvements:**
- ✅ Separation of concerns
- ✅ Type safety
- ✅ Testability
- ✅ Reusability
- ✅ Maintainability

---

## Common Tasks

### Add New API Endpoint

1. **Backend** (`src/http_server.cpp`)
   ```cpp
   routes["/api/new/endpoint"] = [this](const Request& r) {
     return handle_new_endpoint(r);
   };
   ```

2. **Frontend Types** (`src/api/types.ts`)
   ```typescript
   export interface NewEndpointResponse {
     status: string;
     data: any;
   }
   ```

3. **Frontend Client** (`src/api/client.ts`)
   ```typescript
   async newEndpoint(): Promise<NewEndpointResponse> {
     return this.request('POST', '/api/new/endpoint', {});
   }
   ```

4. **Frontend Usage** (`src/main.ts`)
   ```typescript
   const response = await apiClient.newEndpoint();
   ```

### Add New UI Screen

1. **Create render function** in `src/main.ts`
   ```typescript
   function renderNewScreen() {
     app.innerHTML = `<div>Content</div>`;
     // Attach event listeners
     document.getElementById('btn').addEventListener('click', () => {
       // Handle click
     });
   }
   ```

2. **Navigate to it**
   ```typescript
   store.switchView('new-screen');
   renderNewScreen();
   ```

### Debug State Changes

```typescript
// Monitor state in console
import { store } from '@store/state';

store.subscribe((state) => {
  console.log('State updated:', state);
});
```

### Performance Profiling

1. **In Vite dev server:**
   - Open DevTools → Performance
   - Click Record
   - Interact with app
   - Stop and analyze

2. **Bundle analysis:**
   ```bash
   npm run build -- --analyze
   ```

---

## Roadmap

### Phase 2: Component Library
- Extract `Dashboard`, `PatientCard`, `VitalMonitor` into reusable components
- Create component test suite
- Build Storybook for components

### Phase 3: WASM Integration
- Compile inference engine to WASM
- Direct C++ ↔ JavaScript FFI
- 10-100x performance boost for compute

### Phase 4: Advanced Features
- Real-time WebSocket updates
- PWA (Progressive Web App) support
- Offline mode with service worker
- Multi-user collaboration

### Phase 5: Mobile App
- React Native or Flutter wrapper
- Native notifications
- Device hardware integration (pulse oximeter, etc.)

---

## Troubleshooting

### "Cannot find module" errors
```bash
# Clear node_modules and reinstall
rm -rf node_modules package-lock.json
npm install
```

### Port 5173 already in use
```bash
# Use different port
npm run dev -- --port 3000
```

### Tailwind CSS not loading
Vite uses Tailwind via CDN. If offline:
1. Install: `npm install -D tailwindcss`
2. Import: `@import 'tailwindcss/base'`
3. Use: `npm run dev`

### Backend not responding
1. Check server is running: `./build/optic-trigeminal`
2. Check backend logs for errors
3. Verify http://localhost:8080/health responds
4. Check browser Network tab for failed requests

### Build output too large
```bash
# Analyze bundle
npm run build -- --analyze

# Check for unused dependencies
npm audit
npm outdated
```

---

## Resources

- **Vite Docs:** https://vitejs.dev
- **TypeScript:** https://www.typescriptlang.org
- **MDN Web Docs:** https://developer.mozilla.org
- **Tailwind CSS:** https://tailwindcss.com

---

**Next Steps:**
1. Run `npm install` in `web/` directory
2. Start dev server: `npm run dev`
3. Begin backend if not already running
4. Open http://localhost:5173 in browser
5. Sign in and test application

---

*Updated: December 24, 2025*
