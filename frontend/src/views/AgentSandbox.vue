<template>
  <div class="sandbox-view">
    <div class="sandbox-header">
      <router-link to="/" class="back-link">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="15 18 9 12 15 6"/></svg>
      </router-link>
      <h1>Agent Sandbox</h1>
      <span class="subtitle">Try out various AI Agents</span>
    </div>

    <div class="agent-grid">
      <div v-for="(agent, index) in sandboxAgents" :key="agent.id" class="agent-card" :style="{ animation: `cardSlideUp 0.4s var(--ease-out) ${index * 80}ms both` }">
        <div class="card-glow"></div>
        <div class="card-content">
          <div class="card-icon">🤖</div>
          <h3>{{ agent.name }}</h3>
          <p v-if="agent.description">{{ agent.description }}</p>
          <div v-if="agent.tags.length" class="card-tags">
            <span v-for="tag in agent.tags" :key="tag" class="tag">{{ tag }}</span>
          </div>
          <div class="card-stats">
            <span>🏷️ {{ agent.version }}</span>
            <span v-if="agent.host">🌐 {{ agent.host }}:{{ agent.port }}</span>
          </div>
          <button class="try-btn" @click="showSandboxModal">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="5 3 19 12 5 21 5 3"/></svg>
            Try Now
          </button>
        </div>
      </div>
    </div>

    <!-- Sandbox Empty State -->
    <Teleport to="body">
      <Transition name="modal-overlay">
        <div v-if="sandboxModalOpen" class="modal-overlay" @click.self="sandboxModalOpen = false">
          <Transition name="modal-card" appear>
            <div class="modal-card">
              <div class="modal-header">
                <span>Sandbox Mode</span>
                <button class="modal-close" @click="sandboxModalOpen = false">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
                </button>
              </div>
              <div class="modal-body">
                <GlassCard variant="highlight" padding="lg">
                  <EmptyState
                    icon="mdi:flask-outline"
                    title="沙箱模式开发中"
                    description="Agent 沙箱执行功能正在开发中，敬请期待"
                  />
                </GlassCard>
              </div>
            </div>
          </Transition>
        </div>
      </Transition>
    </Teleport>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { getAgents } from '../services/grpc-client'
import GlassCard from '../components/layout/GlassCard.vue'
import EmptyState from '../components/feedback/EmptyState.vue'

interface SandboxAgent {
  id: string
  name: string
  description: string
  tags: string[]
  version: string
  host: string
  port: number
}

const sandboxAgents = ref<SandboxAgent[]>([])
const sandboxModalOpen = ref(false)

onMounted(async () => {
  try {
    const resp = await getAgents()
    sandboxAgents.value = resp.agents.map(a => ({
      id: a.service_name,
      name: a.service_name,
      description: a.metadata?.description || '',
      tags: a.tags || [],
      version: a.version || 'v1',
      host: a.host,
      port: a.port,
    }))
  } catch (e) {
    console.warn('Failed to load agents:', e)
  }
})

function showSandboxModal() {
  sandboxModalOpen.value = true
}
</script>

<style scoped>
@import "../styles/design-tokens.css";

.sandbox-view {
  min-height: 100vh;
  background: var(--bg-primary);
}

.sandbox-header {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 20px 32px;
  background: var(--bg-elevated);
  border-bottom: 1px solid var(--border-default);
}

.back-link { display: flex; color: var(--text-secondary); padding: 4px; border-radius: var(--radius-sm); transition: all 0.15s; }
.back-link:hover { background: var(--glass-bg-hover); color: var(--text-primary); }

.sandbox-header h1 {
  font-size: 22px; font-weight: 700; margin: 0;
  background: var(--brand-gradient);
  -webkit-background-clip: text; -webkit-text-fill-color: transparent; background-clip: text;
}

.subtitle { font-size: 13px; color: var(--text-tertiary); }

.agent-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(340px, 1fr));
  gap: 20px;
  padding: 28px 32px;
  max-width: 1200px;
}

.agent-card {
  position: relative;
  background: var(--bg-elevated);
  border: 1px solid var(--border-default);
  border-radius: var(--radius-lg);
  overflow: hidden;
  transition: all 0.2s ease;
  cursor: pointer;
}

@keyframes cardSlideUp {
  from { opacity: 0; transform: translateY(16px); }
  to { opacity: 1; transform: translateY(0); }
}

.agent-card:hover {
  transform: translateY(-4px);
  box-shadow: var(--shadow-lg);
  border-color: var(--border-brand);
}

.card-glow {
  position: absolute;
  top: 0; left: 0; right: 0;
  height: 3px;
  background: var(--brand-gradient);
  opacity: 0;
  transition: opacity 0.2s;
}

.agent-card:hover .card-glow { opacity: 1; }

.card-content { padding: 24px; }

.card-icon { font-size: 40px; margin-bottom: 12px; }

.card-content h3 { font-size: 17px; font-weight: 700; color: var(--text-primary); margin: 0 0 8px; }
.card-content p { font-size: 13px; color: var(--text-secondary); line-height: 1.5; margin: 0 0 14px; }

.card-tags { display: flex; gap: 6px; flex-wrap: wrap; margin-bottom: 14px; }

.tag { padding: 3px 10px; border-radius: 12px; background: var(--bg-surface); color: var(--text-secondary); font-size: 11px; font-weight: 500; border: 1px solid var(--border-subtle); }

.card-stats {
  display: flex; gap: 14px; padding: 12px 0; border-top: 1px solid var(--border-subtle);
  font-size: 12px; color: var(--text-secondary); margin-bottom: 14px;
}

.try-btn {
  display: flex; align-items: center; justify-content: center; gap: 8px;
  width: 100%; padding: 10px;
  border: 1.5px solid var(--brand-primary); border-radius: var(--radius-md);
  background: transparent; color: var(--brand-primary);
  font-size: 14px; font-weight: 600; cursor: pointer;
  transition: all 0.15s;
}

.try-btn:hover { background: var(--brand-primary); color: #fff; }

/* Modal */
.modal-overlay {
  position: fixed; inset: 0;
  background: rgba(0,0,0,0.6); backdrop-filter: blur(4px);
  display: flex; align-items: center; justify-content: center;
  z-index: var(--z-modal);
}

.modal-overlay-enter-active,
.modal-overlay-leave-active {
  transition: opacity var(--duration-normal) var(--ease-default);
}
.modal-overlay-enter-from,
.modal-overlay-leave-to {
  opacity: 0;
}

.modal-card-enter-active {
  transition: opacity var(--duration-normal) var(--ease-bounce),
              transform var(--duration-normal) var(--ease-bounce);
}
.modal-card-leave-active {
  transition: opacity var(--duration-fast) var(--ease-default),
              transform var(--duration-fast) var(--ease-default);
}
.modal-card-enter-from {
  opacity: 0;
  transform: scale(0.95);
}
.modal-card-leave-to {
  opacity: 0;
  transform: scale(0.95);
}

.modal-card {
  background: var(--bg-elevated); border-radius: var(--radius-lg);
  width: 520px; max-width: 90vw; max-height: 80vh;
  display: flex; flex-direction: column;
  box-shadow: var(--shadow-lg);
  border: 1px solid var(--border-default);
}

.modal-header {
  display: flex; align-items: center; justify-content: space-between;
  padding: 16px 20px; border-bottom: 1px solid var(--border-default);
  font-weight: 600; font-size: 15px; color: var(--text-primary);
}

.modal-close { background: none; border: none; cursor: pointer; color: var(--text-tertiary); padding: 4px; border-radius: 4px; }
.modal-close:hover { color: var(--text-primary); background: var(--glass-bg-hover); }

.modal-body { padding: 20px; }

/* Responsive */
@media (max-width: 1024px) {
  .agent-grid {
    grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
    padding: 20px 24px;
  }
}

@media (max-width: 768px) {
  .sandbox-header {
    padding: 16px 20px;
  }
  .agent-grid {
    grid-template-columns: 1fr;
    padding: 16px 20px;
    gap: 16px;
  }
}

@media (max-width: 480px) {
  .sandbox-header h1 {
    font-size: 18px;
  }
  .subtitle {
    display: none;
  }
  .card-content {
    padding: 18px;
  }
}
</style>
