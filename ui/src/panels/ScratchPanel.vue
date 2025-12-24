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
        component: 'ScratchPanel',
        title: 'Scratch',
        params: props.params,
        bounds: getPanelBounds(root.value)
    })
}
</script>

<template>
    <div ref="root" class="panel panel-scratch">
        <div class="panel-header">
            <h3 class="panel-title">Scratch</h3>
            <button v-if="popoutPanel" class="panel-action" @click="popout">
                Popout
            </button>
        </div>
        <p class="panel-text">Drop notes or quick commands here.</p>
        <div class="panel-chip">Panel {{ params?.index ?? 0 }}</div>
    </div>
</template>
