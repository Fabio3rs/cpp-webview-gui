<script setup>
import { inject, ref } from 'vue'
import HelloWebGL from '../components/HelloWebGL.vue'
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
        component: 'ViewportPanel',
        title: 'Viewport',
        params: props.params,
        bounds: getPanelBounds(root.value)
    })
}
</script>

<template>
    <div ref="root" class="panel panel-viewport">
        <div class="panel-header">
            <h3 class="panel-title">Viewport</h3>
            <button v-if="popoutPanel" class="panel-action" @click="popout">
                Popout
            </button>
        </div>
        <HelloWebGL />
    </div>
</template>
