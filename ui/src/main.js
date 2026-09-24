import { createFrontendApp } from './app_setup'
import { installBinaryEventReceiver } from './binary_rpc'
import { installNativeBindings } from './generated/native-bindings.js'
import { installNativeWindowOpen, installNativeMessageBridge } from './native_window_runtime.js'
import 'dockview-vue/dist/styles/dockview.css'
import './style.css'

installNativeBindings()
installNativeWindowOpen()
installNativeMessageBridge()
installBinaryEventReceiver()

createFrontendApp().mount('#app')
