<template>
  <div class="admin-view">
    <div class="admin-header">
      <h1>Agent 管理</h1>
      <div class="header-actions">
        <span v-if="agentsStore.lastFetched" class="fetch-time">
          {{ timeSince(agentsStore.lastFetched) }} 前更新
        </span>
        <button class="btn-refresh" @click="agentsStore.fetchAgents()" :disabled="agentsStore.loading">
          {{ agentsStore.loading ? '刷新中...' : '刷新' }}
        </button>
        <router-link to="/" class="btn-back">返回聊天</router-link>
      </div>
    </div>

    <div class="admin-content">
      <div v-if="agentsStore.error" class="error-banner">
        {{ agentsStore.error }}
      </div>

      <div v-if="agentsStore.agents.length === 0 && !agentsStore.loading" class="empty-state">
        <p>暂无已注册的 Agent</p>
        <p class="hint">Agent 启动后会自动注册到 Registry，每 15 秒同步一次</p>
      </div>

      <div class="agent-grid">
        <AgentCard
          v-for="agent in agentsStore.agents"
          :key="agent.id"
          :agent="agent"
        />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { onMounted, onUnmounted } from 'vue'
import { useAgentsStore } from '../stores/agents'
import AgentCard from '../components/AgentCard.vue'

const agentsStore = useAgentsStore()

onMounted(() => {
  agentsStore.startPolling(15000)
})

onUnmounted(() => {
  agentsStore.stopPolling()
})

function timeSince(ts: number): string {
  const seconds = Math.floor((Date.now() - ts) / 1000)
  if (seconds < 60) return `${seconds}s`
  const minutes = Math.floor(seconds / 60)
  return `${minutes}min`
}
</script>

<style scoped>
.admin-view {
  display: flex;
  flex-direction: column;
  height: 100vh;
  max-width: 960px;
  margin: 0 auto;
  background: #fff;
}

.admin-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 24px;
  border-bottom: 1px solid #e5e7eb;
}

.admin-header h1 {
  font-size: 20px;
  font-weight: 600;
  margin: 0;
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 12px;
}

.fetch-time {
  font-size: 13px;
  color: #6b7280;
}

.btn-refresh, .btn-back {
  padding: 6px 14px;
  border-radius: 6px;
  font-size: 13px;
  cursor: pointer;
  text-decoration: none;
  border: 1px solid #d1d5db;
  background: #fff;
  color: #374151;
  transition: background 0.15s;
}

.btn-refresh:hover:not(:disabled), .btn-back:hover {
  background: #f3f4f6;
}

.btn-refresh:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.admin-content {
  flex: 1;
  overflow-y: auto;
  padding: 24px;
}

.error-banner {
  padding: 12px 16px;
  margin-bottom: 16px;
  border-radius: 8px;
  background: #fef2f2;
  color: #dc2626;
  font-size: 14px;
}

.empty-state {
  text-align: center;
  padding-top: 80px;
  color: #6b7280;
}

.empty-state .hint {
  font-size: 13px;
  color: #9ca3af;
  margin-top: 8px;
}

.agent-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
  gap: 16px;
}
</style>
