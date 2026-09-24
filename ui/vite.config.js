import { defineConfig } from "vite"
import { viteSingleFile } from "vite-plugin-singlefile"
import vue from "@vitejs/plugin-vue"
import { realpathSync } from "node:fs"
import { fileURLToPath } from "node:url"

function devPort(name, fallback) {
  const value = process.env[name]
  if (value === undefined) return fallback
  const port = Number(value)
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error(`${name} must be a port between 1 and 65535`)
  }
  return port
}

const vitePort = devPort('APP_VITE_PORT', 5173)
const rpcPort = devPort('APP_RPC_PORT', 5174)
if (vitePort === rpcPort) throw new Error('APP_VITE_PORT and APP_RPC_PORT must differ')

const uiRoot = realpathSync(fileURLToPath(new URL(".", import.meta.url)))
  .replaceAll("\\", "/").toLowerCase()
let rootHash = 2166136261
for (const byte of Buffer.from(uiRoot, "utf8")) {
  rootHash = Math.imul(rootHash ^ byte, 16777619) >>> 0
}
const devIdentityPath = `/__app_dev_identity/${rootHash.toString(16).padStart(8, "0")}`

function devIdentity() {
  return {
    name: "app-dev-identity",
    configureServer(server) {
      server.middlewares.use((request, response, next) => {
        if (request.url?.split("?", 1)[0] !== devIdentityPath) {
          next()
          return
        }
        response.statusCode = 204
        response.end()
      })
    }
  }
}

export default defineConfig(({ mode }) => ({
	// ============================================================================
	// Plugins
	// ============================================================================
	plugins: [vue(), viteSingleFile(), devIdentity()],

	// ============================================================================
	// Dev Server (para hot reload no WebView)
	// ============================================================================
	clearScreen: false, // não esconder erros do backend C++
	server: {
		host: "127.0.0.1",   // WebView acessa fácil
		port: vitePort,
		strictPort: true,    // falha se porta ocupada (app nativo precisa saber a porta)
		proxy: {
			'/__native_rpc': {
				target: `http://127.0.0.1:${rpcPort}`,
				changeOrigin: true,
				rewrite: path => path.replace(/^\/__native_rpc/, '')
			}
		},
		watch: {
			// Ignorar arquivos que não são do frontend
			ignored: ["**/dist/**", "**/*.h"],
		},
	},

	// ============================================================================
	// Env vars expostas ao frontend
	// ============================================================================
	envPrefix: ["VITE_", "APP_"],

	// ============================================================================
	// Build (produção)
	// ============================================================================
	base: "./", // IMPORTANTE: permite file:// e esquemas custom (app://)
	build: {
		outDir: "dist",
		assetsDir: "assets",
		// Target alinhado com WebKitGTK / WebView2 modernos
		target: "es2020",
		// Gera manifest.json para integração avançada com C++ (opcional)
		manifest: mode === "production",
		// Minificação
		minify: "esbuild",
		// Source maps só em dev
		sourcemap: mode === "development",
	},

	// ============================================================================
	// Preview server (para testar build de prod localmente)
	// ============================================================================
	preview: {
		host: "127.0.0.1",
		port: 4173,
		strictPort: true,
	},
}))
