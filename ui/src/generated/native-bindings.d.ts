export {};

declare global {
  function ping(arg0: string | null): any;
  function getVersion(): any;
  function openFile(arg0: string): any;
  function getCounter(): number;
  function getPi(): number;
  function getStatus(): string;
  function isReady(): boolean;
  function getConfig(): any;
  function createNativeWindow(arg0: any): string;
  function getBootstrap(arg0: string): any;
  function postNativeEvent(arg0: string, arg1: any): void;
  function closeNativeWindow(arg0: string): void;
  function listNativeWindows(): any;
  function startNativeDrag(arg0: string, arg1: any): void;
  function completeNativeDrag(arg0: string): any;
  function stopNativeDrag(): void;
  function completeNativeDragOutside(arg0: string): any;
}
