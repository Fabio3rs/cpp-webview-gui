import { createApp } from 'vue'
import App from './App.vue'
import { registerDockPanels } from './dock_panels'

// Application-owned Vue setup. Replace the demo registration here when
// starting a new project; main.js contains the reusable native runtime.
export function createFrontendApp() {
  const app = createApp(App)
  registerDockPanels(app)
  return app
}
