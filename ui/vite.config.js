import { defineConfig } from "vite"
import { viteSingleFile } from "vite-plugin-singlefile"
import vue from "@vitejs/plugin-vue"

export default defineConfig(({ mode }) => ({
	// ============================================================================
	// Plugins
	// ============================================================================
	plugins: [vue(), viteSingleFile()],

	// ============================================================================
	// Dev Server (para hot reload no WebView)
	// ============================================================================
	clearScreen: false, // não esconder erros do backend C++
	server: {
		host: "127.0.0.1",   // WebView acessa fácil
		port: 5173,
		strictPort: true,    // falha se porta ocupada (app nativo precisa saber a porta)
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
