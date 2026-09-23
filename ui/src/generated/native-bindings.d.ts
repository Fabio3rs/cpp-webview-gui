export {};

declare global {
  type NativeBindingResult<T> = { ok: true; data?: T } | { ok: false; error: { code: number; message: string } };
  function ping(arg0: string | null): Promise<NativeBindingResult<{ message: string; echo: string }>>;
  function getVersion(): Promise<NativeBindingResult<{ version: string }>>;
  function openFile(arg0: string): Promise<NativeBindingResult<{ path: string; status: string }>>;
  function getCounter(): Promise<NativeBindingResult<number>>;
  function getPi(): Promise<NativeBindingResult<number>>;
  function getStatus(): Promise<NativeBindingResult<string>>;
  function isReady(): Promise<NativeBindingResult<boolean>>;
  function getConfig(): Promise<NativeBindingResult<{ theme: string; lang: string }>>;
  function createNativeWindow(arg0: { [key: string]: unknown; windowId?: string; title?: string; url?: string; width?: number; height?: number; left?: number; top?: number }): Promise<NativeBindingResult<string>>;
  function getBootstrap(arg0: string): Promise<NativeBindingResult<{ [key: string]: unknown; windowId?: string; title?: string; url?: string; width?: number; height?: number; left?: number; top?: number }>>;
  function postNativeEvent(arg0: string, arg1: unknown): Promise<NativeBindingResult<Record<string, never>>>;
  function closeNativeWindow(arg0: string): Promise<NativeBindingResult<Record<string, never>>>;
  function listNativeWindows(): Promise<NativeBindingResult<Array<{ id: string; title: string }>>>;
  function startNativeDrag(arg0: string, arg1: unknown): Promise<NativeBindingResult<Record<string, never>>>;
  function completeNativeDrag(arg0: string): Promise<NativeBindingResult<unknown | null>>;
  function stopNativeDrag(): Promise<NativeBindingResult<Record<string, never>>>;
  function completeNativeDragOutside(arg0: string): Promise<NativeBindingResult<{ payload: unknown; drop?: { x: number; y: number } } | null>>;
}
