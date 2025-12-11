import * as ts from 'typescript/lib/tsserverlibrary';
import * as fs from 'fs';
import * as path from 'path';

interface NativePos {
    file: string;
    line: number;
    column: number;
}

interface NativeBindingLocation {
    begin?: NativePos;
    end?: NativePos;
}

interface NativeBindingsIndex {
    [name: string]: NativeBindingLocation;
}

function init(modules: { typescript: typeof ts }) {
    const tsModule = modules.typescript;

    function create(info: ts.server.PluginCreateInfo): ts.LanguageService {
        const logger = info.project.projectService.logger;
        const config = info.config || {};
        const projectRoot = info.project.getCurrentDirectory();
        const indexPathFromConfig = config.indexPath as string | undefined;

        const indexPath = indexPathFromConfig
            ? path.resolve(projectRoot, indexPathFromConfig)
            : path.resolve(projectRoot, 'src/generated/native-bindings.json');

        let bindingsIndex: NativeBindingsIndex = {};
        try {
            logger.info(`[native-bindings-plugin] attempting to load index at ${indexPath}`);
            const text = fs.readFileSync(indexPath, 'utf8');
            bindingsIndex = JSON.parse(text);
            logger.info(`[native-bindings-plugin] loaded index ${indexPath} (entries=${Object.keys(bindingsIndex).length})`);
        } catch (e) {
            logger.info(`[native-bindings-plugin] could not load index: ${(e as Error).message}`);
        }

        const oldLS = info.languageService;
        const proxy: ts.LanguageService = Object.create(null);

        for (const k of Object.keys(oldLS) as Array<keyof ts.LanguageService>) {
            const x = oldLS[k];
            proxy[k] = (...args: any[]) => (x as any).apply(oldLS, args);
        }

        function getIdentifierAtPosition(fileName: string, position: number): string | undefined {
            const program = info.languageService.getProgram();
            if (!program) return;
            const sf = program.getSourceFile(fileName);
            if (!sf) return;
            const token = (tsModule as any).getTokenAtPosition(sf, position) as any;
            if (!token) return;
            if (token.kind === tsModule.SyntaxKind.Identifier) return token.getText(sf);
            if (position > 0) {
                const tokenBefore = (tsModule as any).getTokenAtPosition(sf, position - 1) as any;
                if (tokenBefore && tokenBefore.kind === tsModule.SyntaxKind.Identifier) return tokenBefore.getText(sf);
            }
            return;
        }



        proxy.getDefinitionAtPosition = (fileName: string, position: number) => {
            logger.info(`[native-bindings-plugin] getDefinitionAtPosition called for ${fileName}@${position}`);
            const original = oldLS.getDefinitionAtPosition(fileName, position) || [];
            const ident = getIdentifierAtPosition(fileName, position);
            logger.info(`[native-bindings-plugin] identifier at pos: ${ident}`);
            if (!ident) return original;
            const nativeLoc = bindingsIndex[ident];
            logger.info(`[native-bindings-plugin] nativeLoc for ${ident}: ${nativeLoc ? JSON.stringify(nativeLoc) : 'not found'}`);
            if (!nativeLoc) return original;

            // Try to compute a precise textSpan from line/column in the index
            const textSpan: ts.TextSpan = { start: 0, length: 0 };
            // Prefer begin position when computing target path/offsets
            const pos = nativeLoc.begin || nativeLoc.end;
            if (!pos) return original;
            try {
                // Resolve cpp file path relative to project root if needed
                let targetPath = path.isAbsolute(pos.file)
                    ? pos.file
                    : path.resolve(info.project.getCurrentDirectory(), pos.file);

                // If the index points to a header, prefer an implementation file in the same dir
                const ext = (path as any).extname(targetPath).toLowerCase();
                if (ext === '.h' || ext === '.hpp' || ext === '.hh') {
                    const base = targetPath.slice(0, -ext.length);
                    const candidates = ['.cpp', '.cc', '.cxx', '.ipp', '.inl'];
                    for (const c of candidates) {
                        const p = base + c;
                        if ((fs as any).existsSync(p)) {
                            logger.info(`[native-bindings-plugin] found implementation fallback ${p} for header ${targetPath}`);
                            targetPath = p;
                            break;
                        }
                    }
                }

                const cppText = fs.readFileSync(targetPath, 'utf8');
                const lines = cppText.split(/\r?\n/);
                const lineIndex = Math.max(0, (pos.line || 1) - 1);
                let offset = 0;
                for (let i = 0; i < lineIndex && i < lines.length; ++i) {
                    offset += lines[i].length + 1; // assume '\n' separator
                }
                const colIndex = Math.max(0, (pos.column || 1) - 1);
                offset += Math.min(colIndex, (lines[lineIndex] || '').length);

                textSpan.start = offset;
                textSpan.length = ident.length || 0;
            } catch (e) {
                // fallback: leave start=0
                logger.info(`[native-bindings-plugin] could not compute textSpan: ${(e as Error).message}`);
            }

            // Ensure we provide an absolute fileName that tsserver can open directly
            const defFile = path.isAbsolute(pos.file)
                ? pos.file
                : path.resolve(info.project.getCurrentDirectory(), pos.file);

            const defInfo: ts.DefinitionInfo = {
                fileName: defFile,
                textSpan,
                kind: tsModule.ScriptElementKind.functionElement,
                name: ident,
                containerName: 'C++ native binding',
                containerKind: tsModule.ScriptElementKind.unknown,
            };

            // Return native definition first so editor opens the C++ location
            return [defInfo, ...original];
        };

        proxy.getDefinitionAndBoundSpan = (fileName, position) => {
            logger.info(`[native-bindings-plugin] getDefinitionAndBoundSpan called for ${fileName}@${position}`);
            const base = oldLS.getDefinitionAndBoundSpan(fileName, position);
            const ident = getIdentifierAtPosition(fileName, position);
            logger.info(`[native-bindings-plugin] identifier at pos: ${ident}`);
            if (!ident) return base;

            const nativeLoc = bindingsIndex[ident];
            logger.info(`[native-bindings-plugin] nativeLoc for ${ident}: ${nativeLoc ? JSON.stringify(nativeLoc) : 'not found'}`);
            if (!nativeLoc) return base;


            // Try to compute a precise textSpan from line/column in the index
            const textSpan: ts.TextSpan = { start: 0, length: 0 };
            const pos = nativeLoc.begin || nativeLoc.end;
            if (!pos) return base;
            try {
                // Resolve cpp file path relative to project root if needed
                let targetPath = path.isAbsolute(pos.file)
                    ? pos.file
                    : path.resolve(info.project.getCurrentDirectory(), pos.file);

                // If the index points to a header, prefer an implementation file in the same dir
                const ext = (path as any).extname(targetPath).toLowerCase();
                if (ext === '.h' || ext === '.hpp' || ext === '.hh') {
                    const base = targetPath.slice(0, -ext.length);
                    const candidates = ['.cpp', '.cc', '.cxx', '.ipp', '.inl'];
                    for (const c of candidates) {
                        const p = base + c;
                        if ((fs as any).existsSync(p)) {
                            logger.info(`[native-bindings-plugin] found implementation fallback ${p} for header ${targetPath}`);
                            targetPath = p;
                            break;
                        }
                    }
                }

                const cppText = fs.readFileSync(targetPath, 'utf8');
                const lines = cppText.split(/\r?\n/);
                const lineIndex = Math.max(0, (pos.line || 1) - 1);
                let offset = 0;
                for (let i = 0; i < lineIndex && i < lines.length; ++i) {
                    offset += lines[i].length + 1; // assume '\n' separator
                }
                const colIndex = Math.max(0, (pos.column || 1) - 1);
                offset += Math.min(colIndex, (lines[lineIndex] || '').length);

                textSpan.start = offset;
                textSpan.length = ident.length || 0;
            } catch (e) {
                // fallback: leave start=0
                logger.info(`[native-bindings-plugin] could not compute textSpan: ${(e as Error).message}`);
            }



            const defFile = path.isAbsolute(pos.file)
                ? pos.file
                : path.resolve(info.project.getCurrentDirectory(), pos.file);


            const defInfo: ts.DefinitionInfo = {
                fileName: defFile,
                textSpan,
                kind: tsModule.ScriptElementKind.functionElement,
                name: ident,
                containerName: 'C++ native binding',
                containerKind: tsModule.ScriptElementKind.unknown,
            };


            if (!base) {
                // fallback: só nossa definição C++
                return {
                    textSpan: { start: position, length: ident.length },
                    definitions: [defInfo],
                };
            }

            // injeta nossa definição como primeira
            return {
                textSpan: base.textSpan,
                definitions: [defInfo, ...(base.definitions ?? [])],
            };
        };

        return proxy;
    }

    return { create };
}

export = init;
