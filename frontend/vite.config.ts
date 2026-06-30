import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  server: {
    proxy: {
      // gRPC-Web 请求代理到 grpcwebproxy（开发时绕过 Nginx）
      '/agent_communication.': {
        target: 'http://localhost:8081',
        changeOrigin: true,
      },
    },
  },
})
