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
        component: 'ConsolePanel',
        title: 'Console',
        params: props.params,
        bounds: getPanelBounds(root.value)
    })
}
</script>

<template>
    <div ref="root" class="panel panel-console">
        <div class="panel-header">
            <h3 class="panel-title">Console</h3>
            <button v-if="popoutPanel" class="panel-action" @click="popout">
                Popout
            </button>
        </div>
        <div class="console-lines">
            <div class="console-line">[info] Dockview layout ready.</div>
            <div class="console-line">[warn] Popout disabled in preview.</div>
            <div class="console-line">[info] Drag a tab to float it.</div>
            <div class="console-line">[info] Layout snapshot saved.</div>
        </div>
    </div>
</template>
