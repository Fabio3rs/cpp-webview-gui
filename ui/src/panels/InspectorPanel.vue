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
        component: 'InspectorPanel',
        title: 'Inspector',
        params: props.params,
        bounds: getPanelBounds(root.value)
    })
}
</script>

<template>
    <div ref="root" class="panel panel-inspector">
        <div class="panel-header">
            <h3 class="panel-title">Inspector</h3>
            <button v-if="popoutPanel" class="panel-action" @click="popout">
                Popout
            </button>
        </div>
        <div class="panel-list">
            <div class="panel-row">
                <span class="panel-label">Selection</span>
                <span class="panel-chip">Camera Rig</span>
            </div>
            <div class="panel-row">
                <span class="panel-label">Depth</span>
                <span class="panel-chip">24.0</span>
            </div>
            <div class="panel-row">
                <span class="panel-label">Blend</span>
                <span class="panel-chip">Soft Light</span>
            </div>
            <div class="panel-row">
                <span class="panel-label">Snap</span>
                <span class="panel-chip">Grid 8px</span>
            </div>
        </div>
    </div>
</template>
