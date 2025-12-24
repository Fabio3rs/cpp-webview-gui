<script setup>
import { inject, ref } from 'vue'
import { getPanelBounds } from './panel_utils'

const popoutPanel = inject('popoutPanel', null)

const root = ref(null)

const props = defineProps({
    params: {
        type: Object,
        default: () => ({})
    }
})

function popout() {
    if (!popoutPanel) return
    popoutPanel({
        component: 'WelcomePanel',
        title: 'Workspace',
        params: props.params,
        bounds: getPanelBounds(root.value)
    })
}
</script>

<template>
    <div ref="root" class="panel panel-welcome">
        <div class="panel-header">
            <h3 class="panel-title">Workspace</h3>
            <button v-if="popoutPanel" class="panel-action" @click="popout">
                Popout
            </button>
        </div>
        <p class="panel-text">Drag tabs to create splits and merge groups.</p>
        <div class="panel-grid">
            <div class="panel-card">
                <span class="panel-label">Scene</span>
                <strong class="panel-value">Orbit Lab</strong>
            </div>
            <div class="panel-card">
                <span class="panel-label">Mode</span>
                <strong class="panel-value">Layout</strong>
            </div>
            <div class="panel-card">
                <span class="panel-label">Panels</span>
                <strong class="panel-value">Dockview</strong>
            </div>
        </div>
    </div>
</template>
