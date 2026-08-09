import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import tailwindcss from '@tailwindcss/vite'

export default defineConfig({
  plugins: [
    vue(),
    tailwindcss(),
  ],
  resolve: {
    alias: {
      '@': '/src',
    },
  },
  server: {
    proxy: {
      // Browser JSON requests go only to the local Node JSON-to-gRPC proxy.
      '/agent_communication.': {
        target: 'http://localhost:8081',
        changeOrigin: true,
      },
    },
  },
})
