<script setup>
import { onBeforeUnmount, onMounted, provide, ref } from 'vue'
import { DockviewVue } from 'dockview-vue'
import { getPanelBounds } from './panels/panel_utils'

const dockApi = ref(null)
let scratchIndex = 1
const dragOverlayVisible = ref(false)
const dragTargets = ref([])
const dragContext = ref(null)
const dragActive = ref(false)
let nativeDragCleanupTimer = null
const pendingDockMoves = []
const dockDisposables = []

function getWindowId() {
    const params = new URLSearchParams(window.location.search)
    return params.get('wid') || window.__APP_WINDOW_ID__ || 'main'
}

async function loadBootstrap() {
    if (!window.getBootstrap) return null
    try {
        const result = await window.getBootstrap(getWindowId())
        if (result?.ok) {
            return result.data
        }
    } catch (error) {
        console.warn('[UI] Falha ao carregar bootstrap:', error)
    }
    return null
}

async function applyBootstrap(api) {
    const bootstrap = await loadBootstrap()
    if (!bootstrap) {
        seedLayout(api)
        return
    }

    if (bootstrap.layout) {
        api.fromJSON(bootstrap.layout)
        return
    }

    if (Array.isArray(bootstrap.panels) && bootstrap.panels.length > 0) {
        api.clear()
        addPanelsToDock(api, bootstrap)
        return
    }

    if (bootstrap.panel && bootstrap.panel.component) {
        const panel = bootstrap.panel
        const panelId =
            panel.id || `${panel.component}-${Date.now().toString(36)}`
        api.clear()
        addPanelsToDock(api, {
            panels: [
                {
                    id: panelId,
                    title: panel.title || panel.component,
                    component: panel.component,
                    params: panel.params || {}
                }
            ],
            activePanelId: panelId
        })
        return
    }

    seedLayout(api)
}

function seedLayout(api) {
    api.clear()
    api.addPanel({
        id: 'welcome',
        title: 'Workspace',
        component: 'WelcomePanel'
    })
    api.addPanel({
        id: 'console',
        title: 'Console',
        component: 'ConsolePanel',
        position: { referencePanel: 'welcome', direction: 'within' }
    })
    api.addPanel({
        id: 'inspector',
        title: 'Inspector',
        component: 'InspectorPanel',
        position: { referencePanel: 'welcome', direction: 'right' }
    })
    api.addPanel({
        id: 'timeline',
        title: 'Timeline',
        component: 'TimelinePanel',
        position: { referencePanel: 'inspector', direction: 'below' }
    })
}

async function onReady(event) {
    dockApi.value = event.api
    await applyBootstrap(event.api)
    registerDockviewDragHandlers(event.api)
    flushPendingMoves(event.api)
}

function addScratch() {
    if (!dockApi.value) return
    const id = `scratch-${scratchIndex++}`
    dockApi.value.addPanel({
        id,
        title: `Scratch ${scratchIndex - 1}`,
        component: 'ScratchPanel',
        params: { index: scratchIndex - 1 }
    })
}

function resetLayout() {
    if (!dockApi.value) return
    seedLayout(dockApi.value)
}

const popoutPanel = async (panel) => {
    const bounds = panel.bounds || {}
    const width = Math.round(
        bounds.width ?? panel.width ?? dockApi.value?.width ?? 1000
    )
    const height = Math.round(
        bounds.height ?? panel.height ?? dockApi.value?.height ?? 700
    )

    if (typeof window.createNativeWindow !== 'function') {
        const left = Number.isFinite(bounds.left)
            ? `,left=${Math.round(bounds.left)}`
            : ''
        const top = Number.isFinite(bounds.top)
            ? `,top=${Math.round(bounds.top)}`
            : ''
        const features = `width=${width},height=${height}${left}${top}`
        window.open(window.location.href, '_blank', features)
        return
    }

    const bootstrap = {
        kind: 'dockview',
        title: panel.title || panel.component || 'Popout',
        width,
        height,
        panel: {
            id: panel.id || null,
            title: panel.title || panel.component,
            component: panel.component,
            params: panel.params || {}
        }
    }

    if (Number.isFinite(bounds.left)) {
        bootstrap.left = Math.round(bounds.left)
    }
    if (Number.isFinite(bounds.top)) {
        bootstrap.top = Math.round(bounds.top)
    }

    try {
        const result = await window.createNativeWindow(bootstrap)
        if (!result?.ok) {
            console.warn('[UI] createNativeWindow falhou:', result?.error)
        }
    } catch (error) {
        console.warn('[UI] Erro ao abrir janela nativa:', error)
    }
}

provide('popoutPanel', popoutPanel)

function normalizePanelState(raw) {
    if (!raw || typeof raw !== 'object') {
        return null
    }
    const component = raw.component || raw.contentComponent
    if (!component) {
        return null
    }
    return {
        id: raw.id,
        title: raw.title,
        component,
        tabComponent: raw.tabComponent,
        renderer: raw.renderer,
        params: raw.params || {},
        minimumWidth: raw.minimumWidth,
        minimumHeight: raw.minimumHeight,
        maximumWidth: raw.maximumWidth,
        maximumHeight: raw.maximumHeight
    }
}

function ensureUniquePanelId(api, baseId) {
    const seed = baseId || `panel-${Date.now().toString(36)}`
    let candidate = seed
    let counter = 1
    while (api.getPanel && api.getPanel(candidate)) {
        candidate = `${seed}-${counter++}`
    }
    return candidate
}

function addPanelsToDock(api, payload, placement = {}) {
    if (!payload || !Array.isArray(payload.panels)) {
        return
    }
    const normalized = payload.panels
        .map(normalizePanelState)
        .filter((panel) => panel && panel.component)
    if (normalized.length === 0) {
        return
    }

    const idMap = new Map()
    let rootId = null
    for (const panel of normalized) {
        const panelId = ensureUniquePanelId(api, panel.id)
        idMap.set(panel.id, panelId)
        const options = {
            id: panelId,
            title: panel.title || panel.component,
            component: panel.component,
            params: panel.params || {},
            tabComponent: panel.tabComponent,
            renderer: panel.renderer,
            minimumWidth: panel.minimumWidth,
            minimumHeight: panel.minimumHeight,
            maximumWidth: panel.maximumWidth,
            maximumHeight: panel.maximumHeight,
            inactive: true
        }

        if (!rootId && placement.referencePanelId) {
            options.position = {
                referencePanel: placement.referencePanelId,
                direction: placement.direction || 'right'
            }
        } else if (rootId) {
            options.position = {
                referencePanel: rootId,
                direction: 'within'
            }
        }

        api.addPanel(options)
        if (!rootId) {
            rootId = panelId
        }
    }

    const activeId = idMap.get(payload.activePanelId) || rootId
    if (activeId) {
        const activePanel = api.getPanel(activeId)
        if (activePanel?.api?.setActive) {
            activePanel.api.setActive()
        }
    }
}

function buildPayloadFromPanel(panel) {
    if (!panel || typeof panel.toJSON !== 'function') {
        return null
    }
    return {
        panels: [panel.toJSON()],
        activePanelId: panel.id
    }
}

function buildPayloadFromGroup(group) {
    if (!group || !Array.isArray(group.panels)) {
        return null
    }
    const panels = group.panels
        .map((panel) => (panel?.toJSON ? panel.toJSON() : null))
        .filter(Boolean)
    if (panels.length === 0) {
        return null
    }
    return {
        panels,
        activePanelId: group.activePanel?.id
    }
}

function resolveBoundsFromPanel(panel) {
    // TODO: Prefer the panel content element when Dockview exposes it in the API.
    const element =
        panel?.view?.content?.element || panel?.group?.element || null
    return getPanelBounds(element)
}

function resolveBoundsFromGroup(group) {
    return getPanelBounds(group?.element || null)
}

async function refreshDragTargets() {
    const targets = []
    const currentId = getWindowId()

    if (typeof window.createNativeWindow === 'function') {
        targets.push({ id: '__new__', title: 'New window', kind: 'new' })
    }

    if (typeof window.listNativeWindows === 'function') {
        try {
            const result = await window.listNativeWindows()
            if (result?.ok && Array.isArray(result.data)) {
                for (const entry of result.data) {
                    if (!entry || entry.id === currentId) continue
                    targets.push({
                        id: entry.id,
                        title: entry.title || entry.id,
                        kind: 'existing'
                    })
                }
            }
        } catch (error) {
            console.warn('[UI] Falha ao listar janelas nativas:', error)
        }
    }

    dragTargets.value = targets
}

function beginDrag(context) {
    dragContext.value = context
    dragActive.value = true
    dragOverlayVisible.value = false
}

function showDragOverlay() {
    if (!dragContext.value || dragContext.value.mode !== 'picker') {
        return
    }
    dragOverlayVisible.value = true
    refreshDragTargets()
}

function clearDragState() {
    dragOverlayVisible.value = false
    dragContext.value = null
    dragActive.value = false
    if (nativeDragCleanupTimer) {
        window.clearTimeout(nativeDragCleanupTimer)
        nativeDragCleanupTimer = null
    }
}

async function handleDragTarget(target) {
    const context = dragContext.value
    if (!context || context.mode !== 'picker' || !target) return

    let moved = false
    if (target.kind === 'new') {
        moved = await openPayloadInNewWindow(context)
    } else if (target.kind === 'existing') {
        moved = await sendPayloadToWindow(target.id, context)
    }

    if (moved) {
        closeSourceAfterMove(context)
    }
    clearDragState()
}

async function openPayloadInNewWindow(context) {
    if (typeof window.createNativeWindow !== 'function') {
        return false
    }

    const payload = context.payload
    if (!payload || !Array.isArray(payload.panels) || payload.panels.length === 0) {
        return false
    }
    const bounds = context.bounds || {}
    const drop = context.drop || {}
    const title =
        payload?.panels?.[0]?.title ||
        payload?.panels?.[0]?.component ||
        payload?.panels?.[0]?.contentComponent ||
        'Dockview'
    const bootstrap = {
        kind: 'dockview',
        title,
        panels: payload.panels,
        activePanelId: payload.activePanelId
    }

    if (Number.isFinite(bounds.width)) {
        bootstrap.width = Math.round(bounds.width)
    }
    if (Number.isFinite(bounds.height)) {
        bootstrap.height = Math.round(bounds.height)
    }
    if (Number.isFinite(drop.x)) {
        bootstrap.left = Math.round(drop.x)
    } else if (Number.isFinite(bounds.left)) {
        bootstrap.left = Math.round(bounds.left)
    }
    if (Number.isFinite(drop.y)) {
        bootstrap.top = Math.round(drop.y)
    } else if (Number.isFinite(bounds.top)) {
        bootstrap.top = Math.round(bounds.top)
    }

    try {
        const result = await window.createNativeWindow(bootstrap)
        if (!result?.ok) {
            console.warn('[UI] createNativeWindow falhou:', result?.error)
            return false
        }
        return true
    } catch (error) {
        console.warn('[UI] Erro ao abrir janela nativa:', error)
        return false
    }
}

async function sendPayloadToWindow(targetId, context) {
    if (typeof window.postNativeEvent !== 'function') {
        return false
    }
    if (!context?.payload?.panels?.length) {
        return false
    }
    try {
        const result = await window.postNativeEvent(targetId, {
            type: 'dock.move',
            payload: context.payload
        })
        if (result?.ok === false) {
            console.warn('[UI] postNativeEvent falhou:', result?.error)
            return false
        }
        return true
    } catch (error) {
        console.warn('[UI] Falha ao enviar para outra janela:', error)
        return false
    }
}

function closeSourceAfterMove(context) {
    // TODO: Optional "copy instead of move" toggle for multi-window workflows.
    if (context.kind === 'panel' && context.source?.api?.close) {
        context.source.api.close()
        return
    }
    if (context.kind === 'group' && context.source?.api?.close) {
        context.source.api.close()
    }
}

function supportsNativeDrag() {
    return (
        typeof window.startNativeDrag === 'function' &&
        typeof window.completeNativeDrag === 'function' &&
        typeof window.stopNativeDrag === 'function'
    )
}

function supportsNativeDragOutside() {
    return (
        supportsNativeDrag() &&
        typeof window.completeNativeDragOutside === 'function'
    )
}

async function beginNativeDrag(context) {
    if (!supportsNativeDrag()) {
        return
    }
    dragContext.value = {
        mode: 'native',
        payload: context.payload,
        bounds: context.bounds || {}
    }
    try {
        const result = await window.startNativeDrag(
            getWindowId(),
            context.payload
        )
        if (result?.ok === false) {
            console.warn('[UI] startNativeDrag falhou:', result?.error)
        }
    } catch (error) {
        console.warn('[UI] Falha ao iniciar drag nativo:', error)
    }
}

async function acceptExternalDrop() {
    if (!supportsNativeDrag()) {
        return
    }
    try {
        const result = await window.completeNativeDrag(getWindowId())
        if (!result?.ok) {
            console.warn('[UI] completeNativeDrag falhou:', result?.error)
            clearDragState()
            return
        }
        if (result.data) {
            applyDockMove(result.data)
        }
    } catch (error) {
        console.warn('[UI] Erro ao completar drag nativo:', error)
    }
    clearDragState()
}

async function completeNativeDragOutside() {
    if (!supportsNativeDragOutside()) {
        return null
    }
    try {
        const result = await window.completeNativeDragOutside(getWindowId())
        if (!result?.ok) {
            console.warn('[UI] completeNativeDragOutside falhou:', result?.error)
            return null
        }
        return result.data || null
    } catch (error) {
        console.warn('[UI] Erro ao completar drag externo:', error)
        return null
    }
}

function scheduleNativeDragCleanup() {
    if (nativeDragCleanupTimer) {
        return
    }
    nativeDragCleanupTimer = window.setTimeout(() => {
        nativeDragCleanupTimer = null
        if (dragActive.value && dragContext.value?.mode === 'native') {
            try {
                window.stopNativeDrag()
            } catch (error) {
                console.warn('[UI] Falha ao limpar drag nativo:', error)
            }
            clearDragState()
        }
    }, 300)
}

async function handleNativeDragEnd() {
    const result = await completeNativeDragOutside()
    const payload = result?.payload || null
    if (payload) {
        const created = await openPayloadInNewWindow({
            payload,
            bounds: dragContext.value?.bounds || {},
            drop: result.drop || {}
        })
        if (created) {
            closePanelsFromPayload(payload)
        }
        clearDragState()
        return
    }
    scheduleNativeDragCleanup()
}

function closePanelsFromPayload(payload) {
    if (!dockApi.value || !payload?.panels) {
        return
    }
    for (const panel of payload.panels) {
        const panelId = panel?.id
        if (!panelId) continue
        const existing = dockApi.value.getPanel(panelId)
        if (existing?.api?.close) {
            existing.api.close()
        }
    }
}

function registerDockviewDragHandlers(api) {
    if (!api?.onWillDragPanel || !api?.onWillDragGroup) {
        return
    }

    // TODO: Show the overlay only after the drag leaves the current dock area.
    // TODO: Use native window hover detection to auto-select drop targets.
    dockDisposables.push(
        api.onWillDragPanel((event) => {
            if (!event?.panel) return
            const payload = buildPayloadFromPanel(event.panel)
            if (!payload) return
            dragActive.value = true
            if (supportsNativeDrag()) {
                beginNativeDrag({
                    payload,
                    bounds: resolveBoundsFromPanel(event.panel)
                })
                return
            }
            beginDrag({
                mode: 'picker',
                kind: 'panel',
                source: event.panel,
                payload,
                bounds: resolveBoundsFromPanel(event.panel)
            })
        })
    )

    dockDisposables.push(
        api.onWillDragGroup((event) => {
            if (!event?.group) return
            const payload = buildPayloadFromGroup(event.group)
            if (!payload) return
            dragActive.value = true
            if (supportsNativeDrag()) {
                beginNativeDrag({
                    payload,
                    bounds: resolveBoundsFromGroup(event.group)
                })
                return
            }
            beginDrag({
                mode: 'picker',
                kind: 'group',
                source: event.group,
                payload,
                bounds: resolveBoundsFromGroup(event.group)
            })
        })
    )
}

function applyDockMove(payload) {
    if (!dockApi.value) {
        pendingDockMoves.push(payload)
        return
    }
    if (!payload || !Array.isArray(payload.panels) || payload.panels.length === 0) {
        return
    }
    const referencePanelId = dockApi.value.activePanel?.id
    addPanelsToDock(dockApi.value, payload, {
        referencePanelId,
        direction: referencePanelId ? 'right' : undefined
    })
}

function flushPendingMoves(api) {
    if (!api || pendingDockMoves.length === 0) {
        return
    }
    const moves = pendingDockMoves.splice(0, pendingDockMoves.length)
    for (const payload of moves) {
        if (!payload) continue
        addPanelsToDock(api, payload, {
            referencePanelId: api.activePanel?.id,
            direction: api.activePanel ? 'right' : undefined
        })
    }
}

function handleNativeEvent(event) {
    const detail = event?.detail
    if (!detail) {
        return
    }
    if (detail.type === 'dock.move') {
        applyDockMove(detail.payload)
        return
    }
    if (detail.type === 'dock.dragHover') {
        if (detail.payload?.originWindowId === getWindowId()) {
            return
        }
        dragContext.value = {
            mode: 'drop',
            originWindowId: detail.payload?.originWindowId || ''
        }
        dragOverlayVisible.value = true
        return
    }
    if (detail.type === 'dock.dragLeave') {
        if (dragContext.value?.mode === 'drop') {
            clearDragState()
        }
        return
    }
    if (detail.type === 'dock.dragComplete') {
        closePanelsFromPayload(detail.payload?.dragPayload)
        clearDragState()
    }
}

async function handleGlobalDragEnd() {
    if (!dragActive.value) {
        return
    }
    if (supportsNativeDrag()) {
        await handleNativeDragEnd()
        return
    }
    clearDragState()
}

function handleGlobalKeydown(event) {
    if (event.key === 'Escape' && (dragOverlayVisible.value || dragActive.value)) {
        if (dragActive.value && supportsNativeDrag()) {
            try {
                window.stopNativeDrag()
            } catch (error) {
                console.warn('[UI] Falha ao cancelar drag nativo:', error)
            }
        }
        clearDragState()
    }
}

function didLeaveWindow(event) {
    if (!event || event.relatedTarget) {
        return false
    }
    const { clientX, clientY } = event
    if (
        clientX <= 0 ||
        clientY <= 0 ||
        clientX >= window.innerWidth ||
        clientY >= window.innerHeight
    ) {
        return true
    }
    return event.target === document.documentElement || event.target === document.body
}

function handleGlobalDragLeave(event) {
    if (!dragActive.value || dragOverlayVisible.value) {
        return
    }
    if (supportsNativeDrag()) {
        return
    }
    if (didLeaveWindow(event)) {
        showDragOverlay()
    }
}

onMounted(() => {
    window.addEventListener('native-event', handleNativeEvent)
    window.addEventListener('dragend', handleGlobalDragEnd)
    window.addEventListener('dragleave', handleGlobalDragLeave)
    window.addEventListener('keydown', handleGlobalKeydown)
})

onBeforeUnmount(() => {
    window.removeEventListener('native-event', handleNativeEvent)
    window.removeEventListener('dragend', handleGlobalDragEnd)
    window.removeEventListener('dragleave', handleGlobalDragLeave)
    window.removeEventListener('keydown', handleGlobalKeydown)
    for (const disposable of dockDisposables) {
        if (disposable?.dispose) {
            disposable.dispose()
        }
    }
})
</script>

<template>
    <div class="app-shell">
        <header class="app-header">
            <div class="brand">
                <span class="brand-mark">DV</span>
                <div class="brand-text">
                    <div class="brand-title">Dockview Playground</div>
                    <div class="brand-subtitle">Vue preview for dockable panels</div>
                </div>
            </div>
            <div class="toolbar">
                <button class="toolbar-btn" @click="addScratch">Add panel</button>
                <button class="toolbar-btn ghost" @click="resetLayout">Reset layout</button>
            </div>
        </header>
        <DockviewVue class="dockview-root dockview-theme-abyss" @ready="onReady" />
        <div
            v-if="dragOverlayVisible"
            class="dock-overlay"
            @dragover.prevent
        >
            <div class="dock-overlay-card">
                <div class="dock-overlay-title">
                    {{ dragContext?.mode === 'drop' ? 'Drop to move' : 'Move to window' }}
                </div>
                <div v-if="dragContext?.mode === 'drop'" class="dock-overlay-targets">
                    <button
                        class="dock-overlay-target primary"
                        @click="acceptExternalDrop"
                        @dragover.prevent
                        @drop.prevent="acceptExternalDrop"
                    >
                        <span class="dock-overlay-target-title">Move here</span>
                        <span class="dock-overlay-target-meta">Drop</span>
                    </button>
                </div>
                <div v-else class="dock-overlay-targets">
                    <button
                        v-for="target in dragTargets"
                        :key="target.id"
                        class="dock-overlay-target"
                        :class="{ primary: target.kind === 'new' }"
                        @click="handleDragTarget(target)"
                        @dragover.prevent
                        @drop.prevent="handleDragTarget(target)"
                    >
                        <span class="dock-overlay-target-title">{{ target.title }}</span>
                        <span class="dock-overlay-target-meta">
                            {{ target.kind === 'new' ? 'Create' : 'Send' }}
                        </span>
                    </button>
                    <div v-if="dragTargets.length === 0" class="dock-overlay-empty">
                        No native windows available
                    </div>
                </div>
                <div class="dock-overlay-hint">Drop to move - Esc to cancel</div>
            </div>
        </div>
    </div>
</template>

<style>
.app-shell {
    display: flex;
    flex-direction: column;
    height: 100vh;
    padding: 1.25rem;
    gap: 1rem;
}

.app-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0.75rem 1rem;
    border-radius: 16px;
    background: linear-gradient(135deg, rgba(25, 32, 47, 0.9), rgba(10, 12, 20, 0.9));
    border: 1px solid rgba(110, 140, 180, 0.2);
    box-shadow: 0 16px 40px rgba(3, 8, 16, 0.6);
}

.brand {
    display: flex;
    align-items: center;
    gap: 0.75rem;
}

.brand-mark {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 38px;
    height: 38px;
    border-radius: 12px;
    background: linear-gradient(145deg, #8bc6ff, #3b68d1);
    color: #0b0e13;
    font-weight: 700;
    letter-spacing: 0.06em;
}

.brand-text {
    display: flex;
    flex-direction: column;
    gap: 0.1rem;
}

.brand-title {
    font-size: 1rem;
    font-weight: 600;
}

.brand-subtitle {
    font-size: 0.8rem;
    color: rgba(233, 237, 242, 0.7);
}

.toolbar {
    display: flex;
    align-items: center;
    gap: 0.5rem;
}

.toolbar-btn {
    border: none;
    border-radius: 999px;
    padding: 0.55rem 1rem;
    background: #3b68d1;
    color: #f4f7ff;
    font-weight: 600;
    cursor: pointer;
    transition: transform 0.2s ease, box-shadow 0.2s ease, background 0.2s ease;
}

.toolbar-btn:hover {
    background: #4c7df0;
    box-shadow: 0 10px 20px rgba(59, 104, 209, 0.35);
    transform: translateY(-1px);
}

.toolbar-btn.ghost {
    background: rgba(75, 88, 120, 0.4);
    color: #d9e2f2;
}

.dockview-root {
    flex: 1;
    border-radius: 18px;
    overflow: hidden;
    border: 1px solid rgba(86, 101, 137, 0.3);
    background: rgba(8, 11, 18, 0.6);
    box-shadow: 0 20px 45px rgba(2, 6, 12, 0.6);
    min-height: 420px;
}

.panel {
    height: 100%;
    padding: 1rem 1.1rem;
    color: #e9edf2;
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
}

.panel-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.5rem;
}

.panel-title {
    font-size: 0.95rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: rgba(233, 237, 242, 0.7);
}

.panel-action {
    border: 1px solid rgba(95, 122, 170, 0.4);
    background: rgba(15, 22, 36, 0.8);
    color: rgba(233, 237, 242, 0.85);
    border-radius: 999px;
    padding: 0.2rem 0.7rem;
    font-size: 0.7rem;
    letter-spacing: 0.08em;
    text-transform: uppercase;
    cursor: pointer;
    transition: border 0.2s ease, color 0.2s ease, background 0.2s ease;
}

.panel-action:hover {
    border-color: rgba(120, 160, 220, 0.7);
    background: rgba(25, 35, 55, 0.9);
    color: #f0f4ff;
}

.panel-text {
    color: rgba(233, 237, 242, 0.8);
    font-size: 0.9rem;
}

.panel-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
    gap: 0.75rem;
}

.panel-card {
    background: rgba(18, 24, 38, 0.8);
    border-radius: 12px;
    padding: 0.75rem;
    border: 1px solid rgba(97, 120, 156, 0.25);
}

.panel-label {
    font-size: 0.7rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: rgba(233, 237, 242, 0.55);
}

.panel-value {
    display: block;
    font-size: 0.95rem;
    margin-top: 0.25rem;
}

.panel-list {
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
}

.panel-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0.45rem 0.75rem;
    border-radius: 10px;
    background: rgba(17, 22, 34, 0.8);
}

.panel-chip {
    background: rgba(63, 96, 170, 0.35);
    border-radius: 999px;
    padding: 0.2rem 0.6rem;
    font-size: 0.8rem;
}

.timeline {
    display: flex;
    flex-direction: column;
    gap: 0.9rem;
}

.timeline-track {
    display: grid;
    grid-template-columns: 80px 1fr;
    gap: 0.6rem;
    align-items: center;
}

.track-name {
    font-size: 0.8rem;
    color: rgba(233, 237, 242, 0.6);
}

.track-bar {
    position: relative;
    height: 18px;
    border-radius: 999px;
    background: rgba(32, 44, 68, 0.8);
}

.track-key {
    position: absolute;
    top: 3px;
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background: #86c5ff;
    box-shadow: 0 0 0 2px rgba(10, 16, 30, 0.8);
}

.key-1 { left: 12%; }
.key-2 { left: 36%; background: #f6c356; }
.key-3 { left: 62%; background: #78f0d5; }
.key-4 { left: 78%; background: #f08c8c; }
.key-5 { left: 90%; background: #b88cff; }

.console-lines {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
    font-family: "JetBrains Mono", "Fira Code", monospace;
    font-size: 0.85rem;
    color: rgba(217, 226, 242, 0.75);
}

.panel-scratch .panel-chip {
    width: fit-content;
}

.dock-overlay {
    position: fixed;
    inset: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    background: rgba(8, 12, 20, 0.7);
    backdrop-filter: blur(6px);
    z-index: 40;
    pointer-events: none;
}

.dock-overlay-card {
    width: min(420px, 90vw);
    background: rgba(12, 18, 28, 0.95);
    border: 1px solid rgba(120, 150, 200, 0.25);
    border-radius: 18px;
    padding: 1.25rem;
    box-shadow: 0 20px 45px rgba(2, 6, 12, 0.7);
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
    pointer-events: auto;
}

.dock-overlay-title {
    font-size: 0.85rem;
    text-transform: uppercase;
    letter-spacing: 0.16em;
    color: rgba(204, 220, 255, 0.75);
}

.dock-overlay-targets {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
}

.dock-overlay-target {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.75rem;
    border-radius: 12px;
    padding: 0.6rem 0.85rem;
    border: 1px solid rgba(100, 120, 160, 0.35);
    background: rgba(16, 24, 38, 0.9);
    color: #e9edf2;
    cursor: pointer;
    font-size: 0.85rem;
    transition: border 0.2s ease, background 0.2s ease, transform 0.2s ease;
}

.dock-overlay-target.primary {
    border-color: rgba(130, 170, 255, 0.55);
    background: rgba(30, 52, 98, 0.85);
}

.dock-overlay-target:hover {
    transform: translateY(-1px);
    border-color: rgba(140, 190, 255, 0.7);
    background: rgba(28, 40, 64, 0.95);
}

.dock-overlay-target-title {
    font-weight: 600;
}

.dock-overlay-target-meta {
    font-size: 0.7rem;
    text-transform: uppercase;
    letter-spacing: 0.12em;
    color: rgba(202, 214, 240, 0.7);
}

.dock-overlay-empty {
    padding: 0.6rem 0.75rem;
    border-radius: 12px;
    border: 1px dashed rgba(110, 130, 170, 0.4);
    color: rgba(202, 214, 240, 0.7);
    font-size: 0.8rem;
}

.dock-overlay-hint {
    font-size: 0.75rem;
    color: rgba(197, 208, 230, 0.6);
}
</style>
