import * as ts from 'typescript/lib/tsserverlibrary';
import * as fs from 'fs';
import * as path from 'path';

interface NativeBindingLocation {
  file: string;
  line: number;
  column: number;
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
      const text = fs.readFileSync(indexPath, 'utf8');
      bindingsIndex = JSON.parse(text);
      logger.info(`[native-bindings-plugin] loaded index ${indexPath}`);
    } catch (e) {
      logger.info(`[native-bindings-plugin] could not load index: ${(e as Error).message}`);
    }

    const oldLS = info.languageService;
    const proxy: ts.LanguageService = Object.create(null);

    for (const k of Object.keys(oldLS) as Array<keyof ts.LanguageService>) {
      const x = oldLS[k];
      // @ts-expect-error
      proxy[k] = (...args: any[]) => (x as any).apply(oldLS, args);
    }

    function getIdentifierAtPosition(fileName: string, position: number): string | undefined {
      const program = info.languageService.getProgram();
      if (!program) return;
      const sf = program.getSourceFile(fileName);
      if (!sf) return;
      const token = tsModule.getTokenAtPosition(sf, position);
      if (!token) return;
      if (token.kind === tsModule.SyntaxKind.Identifier) return token.getText(sf);
      if (position > 0) {
        const tokenBefore = tsModule.getTokenAtPosition(sf, position - 1);
        if (tokenBefore && tokenBefore.kind === tsModule.SyntaxKind.Identifier) return tokenBefore.getText(sf);
      }
      return;
    }

    proxy.getDefinitionAtPosition = (fileName: string, position: number) => {
      const original = oldLS.getDefinitionAtPosition(fileName, position) || [];
      const ident = getIdentifierAtPosition(fileName, position);
      if (!ident) return original;
      const nativeLoc = bindingsIndex[ident];
      if (!nativeLoc) return original;

      const textSpan: ts.TextSpan = { start: 0, length: 0 };

      const defInfo: ts.DefinitionInfo = {
        fileName: nativeLoc.file,
        textSpan,
        kind: tsModule.ScriptElementKind.functionElement,
        name: ident,
        containerName: 'C++ native binding',
        containerKind: tsModule.ScriptElementKind.unknown,
      };

      return [...original, defInfo];
    };

    return proxy;
  }

  return { create };
}

export = init;
