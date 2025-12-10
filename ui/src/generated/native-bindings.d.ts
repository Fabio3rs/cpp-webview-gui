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
}
