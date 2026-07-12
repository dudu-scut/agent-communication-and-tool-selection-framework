<template>
  <div class="sandbox-view">
    <div class="sandbox-header">
      <router-link to="/" class="back-link">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="15 18 9 12 15 6"/></svg>
      </router-link>
      <h1>Agent 沙箱</h1>
      <span class="subtitle">快速试用各类 AI Agent</span>
    </div>

    <div class="agent-grid">
      <div v-for="agent in sandboxAgents" :key="agent.id" class="agent-card">
        <div class="card-glow"></div>
        <div class="card-content">
          <div class="card-icon">{{ agent.icon }}</div>
          <h3>{{ agent.name }}</h3>
          <p>{{ agent.description }}</p>
          <div class="card-tags">
            <span v-for="tag in agent.tags" :key="tag" class="tag">{{ tag }}</span>
          </div>
          <div class="card-stats">
            <span>⚡ {{ agent.avgLatency }}ms</span>
            <span>⭐ {{ agent.rating }}/5</span>
            <span>👥 {{ formatCount(agent.usageCount) }}</span>
          </div>
          <button class="try-btn" @click="quickTry(agent)">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="5 3 19 12 5 21 5 3"/></svg>
            快速试用
          </button>
        </div>
      </div>
    </div>

    <!-- Quick Try Modal -->
    <Teleport to="body">
      <div v-if="activeAgent" class="modal-overlay" @click.self="activeAgent = null">
        <div class="modal-card">
          <div class="modal-header">
            <span>{{ activeAgent.icon }} {{ activeAgent.name }} — 试用</span>
            <button class="modal-close" @click="activeAgent = null">
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
            </button>
          </div>
          <div class="modal-body">
            <div v-if="sandboxResponse" class="sandbox-response">{{ sandboxResponse }}</div>
            <div v-else class="sandbox-placeholder">
              <textarea v-model="sandboxInput" placeholder="输入试用消息..." class="sandbox-textarea" rows="3"></textarea>
              <button class="btn-send" @click="sendSandboxQuery" :disabled="!sandboxInput.trim()">
                发送试用
              </button>
            </div>
          </div>
        </div>
      </div>
    </Teleport>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'

interface SandboxAgent {
  id: string
  name: string
  icon: string
  description: string
  tags: string[]
  avgLatency: number
  rating: number
  usageCount: number
}

const sandboxAgents = ref<SandboxAgent[]>([
  { id: 'mock-general', name: 'General Assistant', icon: '🤖', description: '通用 AI 助手，可回答各类问题、编写代码、分析数据', tags: ['对话', '编程', '分析'], avgLatency: 145, rating: 4.7, usageCount: 12500 },
  { id: 'mock-translator', name: 'Translator Pro', icon: '🌐', description: '专业翻译 Agent，支持 50+ 语言互译，保持语境和术语一致性', tags: ['翻译', '本地化', '术语管理'], avgLatency: 89, rating: 4.5, usageCount: 8900 },
  { id: 'mock-writer', name: 'Content Writer', icon: '✍️', description: '内容创作 Agent，擅长博客、报告、营销文案和创意写作', tags: ['写作', '创意', '营销'], avgLatency: 230, rating: 4.8, usageCount: 6700 },
  { id: 'mock-coder', name: 'Code Reviewer', icon: '🔍', description: '代码审查 Agent，自动检测 Bug、安全漏洞和性能问题', tags: ['代码审查', '安全', '性能'], avgLatency: 310, rating: 4.6, usageCount: 15200 },
  { id: 'mock-analyst', name: 'Data Analyst', icon: '📊', description: '数据分析 Agent，生成可视化图表和洞察报告', tags: ['数据', '可视化', '报告'], avgLatency: 420, rating: 4.4, usageCount: 4300 },
  { id: 'mock-planner', name: 'Task Planner', icon: '📋', description: '任务规划 Agent，将复杂任务分解为可执行的子任务 DAG', tags: ['规划', '编排', 'DAG'], avgLatency: 180, rating: 4.9, usageCount: 9800 },
])

const activeAgent = ref<SandboxAgent | null>(null)
const sandboxInput = ref('')
const sandboxResponse = ref('')

function quickTry(agent: SandboxAgent) {
  activeAgent.value = agent
  sandboxInput.value = ''
  sandboxResponse.value = ''
}

async function sendSandboxQuery() {
  if (!sandboxInput.value.trim()) return
  sandboxResponse.value = '处理中...'
  // Simulate sandbox query
  await new Promise(r => setTimeout(r, 1000))
  sandboxResponse.value = `[沙箱模式] Mock 响应: ${sandboxInput.value}\n\n这是来自 "${activeAgent.value?.name}" 的试用回复。沙箱模式不消耗 Token，不保存历史记录。`
}

function formatCount(n: number): string {
  if (n >= 10000) return (n / 1000).toFixed(1) + 'k'
  if (n >= 1000) return (n / 1000).toFixed(1) + 'k'
  return n.toString()
}
</script>

<style scoped>
.sandbox-view {
  min-height: 100vh;
  background: #f8fafc;
}

.sandbox-header {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 20px 32px;
  background: #fff;
  border-bottom: 1px solid #e5e7eb;
}

.back-link { display: flex; color: #6b7280; padding: 4px; border-radius: 6px; }
.back-link:hover { background: #f3f4f6; color: #374151; }

.sandbox-header h1 {
  font-size: 22px; font-weight: 700; margin: 0;
  background: linear-gradient(135deg, #6366f1, #a855f7);
  -webkit-background-clip: text; -webkit-text-fill-color: transparent; background-clip: text;
}

.subtitle { font-size: 13px; color: #9ca3af; }

.agent-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(340px, 1fr));
  gap: 20px;
  padding: 28px 32px;
  max-width: 1200px;
}

.agent-card {
  position: relative;
  background: #fff;
  border: 1px solid #e5e7eb;
  border-radius: 16px;
  overflow: hidden;
  transition: all 0.2s ease;
  cursor: pointer;
}

.agent-card:hover {
  transform: translateY(-4px);
  box-shadow: 0 12px 40px rgba(0,0,0,0.08);
  border-color: #c7d2fe;
}

.card-glow {
  position: absolute;
  top: 0; left: 0; right: 0;
  height: 3px;
  background: linear-gradient(90deg, #6366f1, #a855f7, #ec4899);
  opacity: 0;
  transition: opacity 0.2s;
}

.agent-card:hover .card-glow { opacity: 1; }

.card-content { padding: 24px; }

.card-icon { font-size: 40px; margin-bottom: 12px; }

.card-content h3 { font-size: 17px; font-weight: 700; color: #1f2937; margin: 0 0 8px; }
.card-content p { font-size: 13px; color: #6b7280; line-height: 1.5; margin: 0 0 14px; }

.card-tags { display: flex; gap: 6px; flex-wrap: wrap; margin-bottom: 14px; }

.tag { padding: 3px 10px; border-radius: 12px; background: #f3f4f6; color: #6b7280; font-size: 11px; font-weight: 500; }

.card-stats {
  display: flex; gap: 14px; padding: 12px 0; border-top: 1px solid #f3f4f6;
  font-size: 12px; color: #9ca3af; margin-bottom: 14px;
}

.try-btn {
  display: flex; align-items: center; justify-content: center; gap: 8px;
  width: 100%; padding: 10px;
  border: 1.5px solid #6366f1; border-radius: 10px;
  background: transparent; color: #6366f1;
  font-size: 14px; font-weight: 600; cursor: pointer;
  transition: all 0.15s;
}

.try-btn:hover { background: #6366f1; color: #fff; }

/* Modal */
.modal-overlay {
  position: fixed; inset: 0;
  background: rgba(0,0,0,0.4); backdrop-filter: blur(4px);
  display: flex; align-items: center; justify-content: center;
  z-index: 1000;
}

.modal-card {
  background: #fff; border-radius: 16px;
  width: 520px; max-width: 90vw; max-height: 80vh;
  display: flex; flex-direction: column;
  box-shadow: 0 20px 60px rgba(0,0,0,0.15);
}

.modal-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 16px 20px; border-bottom: 1px solid #e5e7eb;
  font-weight: 600; font-size: 15px;
}

.modal-close { background: none; border: none; cursor: pointer; color: #9ca3af; padding: 4px; border-radius: 4px; }
.modal-close:hover { color: #374151; background: #f3f4f6; }

.modal-body { padding: 20px; }

.sandbox-response {
  padding: 14px; background: #f3f4f6; border-radius: 10px;
  font-size: 14px; line-height: 1.6; white-space: pre-wrap;
}

.sandbox-textarea {
  width: 100%; padding: 12px; border: 1px solid #d1d5db; border-radius: 10px;
  font-size: 14px; font-family: inherit; resize: vertical; outline: none;
}

.sandbox-textarea:focus { border-color: #6366f1; box-shadow: 0 0 0 3px rgba(99,102,241,0.1); }

.btn-send {
  margin-top: 12px; padding: 10px 20px;
  border: none; border-radius: 10px;
  background: #6366f1; color: #fff;
  font-size: 14px; font-weight: 600; cursor: pointer;
}

.btn-send:disabled { background: #c7d2fe; cursor: not-allowed; }
.btn-send:hover:not(:disabled) { background: #4f46e5; }
</style>
