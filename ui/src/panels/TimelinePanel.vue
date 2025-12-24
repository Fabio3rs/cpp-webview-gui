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
        component: 'TimelinePanel',
        title: 'Timeline',
        params: props.params,
        bounds: getPanelBounds(root.value)
    })
}
</script>

<template>
    <div ref="root" class="panel panel-timeline">
        <div class="panel-header">
            <h3 class="panel-title">Timeline</h3>
            <button v-if="popoutPanel" class="panel-action" @click="popout">
                Popout
            </button>
        </div>
        <div class="timeline">
            <div class="timeline-track">
                <span class="track-name">Camera</span>
                <div class="track-bar">
                    <span class="track-key key-1"></span>
                    <span class="track-key key-2"></span>
                    <span class="track-key key-3"></span>
                </div>
            </div>
            <div class="timeline-track">
                <span class="track-name">Lights</span>
                <div class="track-bar">
                    <span class="track-key key-2"></span>
                    <span class="track-key key-4"></span>
                </div>
            </div>
            <div class="timeline-track">
                <span class="track-name">FX</span>
                <div class="track-bar">
                    <span class="track-key key-1"></span>
                    <span class="track-key key-4"></span>
                    <span class="track-key key-5"></span>
                </div>
            </div>
        </div>
    </div>
</template>
