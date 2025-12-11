declare module 'fs' {
    export function readFileSync(path: string, encoding?: string): string;
}

declare module 'path' {
    export function resolve(...paths: string[]): string;
    export function isAbsolute(p: string): boolean;
}
