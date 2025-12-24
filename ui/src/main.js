import { createApp } from 'vue'
import App from './App.vue'
import { registerDockPanels } from './dock_panels'
import 'dockview-vue/dist/styles/dockview.css'
import './style.css'

function parseWindowFeatures(features) {
  if (!features || typeof features !== 'string') {
    return {}
  }
  const result = {}
  for (const part of features.split(',')) {
    const [rawKey, rawValue] = part.split('=')
    if (!rawKey || rawValue === undefined) continue
    const key = rawKey.trim().toLowerCase()
    const value = rawValue.trim()
    const numberValue = Number(value)
    result[key] = Number.isFinite(numberValue) ? numberValue : value
  }
  return result
}

function createWindowProxy(createPromise) {
  let windowId = null
  let closed = false
  const pendingEvents = []

  const resolveId = async () => {
    if (windowId || !createPromise) return windowId
    const result = await createPromise
    if (!result?.ok) {
      closed = true
      return null
    }
    windowId = result.data
    while (pendingEvents.length > 0) {
      const event = pendingEvents.shift()
      if (event && window.postNativeEvent) {
        window.postNativeEvent(windowId, event)
      }
    }
    return windowId
  }

  const sendEvent = (event) => {
    if (!window.postNativeEvent) return
    if (windowId) {
      window.postNativeEvent(windowId, event)
    } else {
      pendingEvents.push(event)
      resolveId()
    }
  }

  return {
    get closed() {
      return closed
    },
    close() {
      closed = true
      resolveId().then((id) => {
        if (id && window.closeNativeWindow) {
          window.closeNativeWindow(id)
        }
      })
    },
    focus() {
      sendEvent({ type: 'focus.window' })
    },
    postMessage(message, targetOrigin) {
      sendEvent({
        type: 'message',
        payload: message,
        origin: targetOrigin || window.location.origin
      })
    }
  }
}

function installNativeWindowOpen() {
  if (window.__nativeWindowOpenInstalled) return
  window.__nativeWindowOpenInstalled = true
  const originalOpen = typeof window.open === 'function'
    ? window.open.bind(window)
    : null

  window.open = (url = '', target = '', features = '') => {
    if (!window.createNativeWindow) {
      return originalOpen ? originalOpen(url, target, features) : null
    }

    const resolvedUrl = url
      ? new URL(url, window.location.href).toString()
      : ''
    const parsed = parseWindowFeatures(features)
    const title = target && !target.startsWith('_') ? target : undefined
    const bootstrap = {}

    if (title) bootstrap.title = title
    if (Number.isFinite(parsed.width)) bootstrap.width = Math.round(parsed.width)
    if (Number.isFinite(parsed.height)) bootstrap.height = Math.round(parsed.height)
    if (Number.isFinite(parsed.left)) bootstrap.left = Math.round(parsed.left)
    if (Number.isFinite(parsed.top)) bootstrap.top = Math.round(parsed.top)
    if (resolvedUrl) bootstrap.url = resolvedUrl

    const promise = window.createNativeWindow(bootstrap)
    return createWindowProxy(promise)
  }
}

function installNativeMessageBridge() {
  if (window.__nativeMessageBridgeInstalled) return
  window.__nativeMessageBridgeInstalled = true
  window.addEventListener('native-event', (event) => {
    const detail = event?.detail
    if (!detail || detail.type !== 'message') return
    window.dispatchEvent(
      new MessageEvent('message', {
        data: detail.payload,
        origin: detail.origin || ''
      })
    )
  })
}

installNativeWindowOpen()
installNativeMessageBridge()

const app = createApp(App)
registerDockPanels(app)
app.mount('#app')
