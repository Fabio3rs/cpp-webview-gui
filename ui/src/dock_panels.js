import WelcomePanel from './panels/WelcomePanel.vue'
import InspectorPanel from './panels/InspectorPanel.vue'
import TimelinePanel from './panels/TimelinePanel.vue'
import ConsolePanel from './panels/ConsolePanel.vue'
import ScratchPanel from './panels/ScratchPanel.vue'

export {
    WelcomePanel,
    InspectorPanel,
    TimelinePanel,
    ConsolePanel,
    ScratchPanel
}

export function registerDockPanels(app) {
    app.component('WelcomePanel', WelcomePanel)
    app.component('InspectorPanel', InspectorPanel)
    app.component('TimelinePanel', TimelinePanel)
    app.component('ConsolePanel', ConsolePanel)
    app.component('ScratchPanel', ScratchPanel)
}
