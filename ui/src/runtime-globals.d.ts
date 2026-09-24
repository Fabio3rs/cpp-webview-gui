declare module '*.vue' {
  import type { DefineComponent } from 'vue'
  const component: DefineComponent<Record<string, never>, Record<string, never>, unknown>
  export default component
}

interface Window {
  __APP_BINARY_RPC__?: { endpoint: string; token?: string }
  __APP_NATIVE_EVENT__?: (token: number) => void
  __nativeWindowOpenInstalled?: boolean
  __nativeMessageBridgeInstalled?: boolean
}
