<script setup lang="ts">
import GlassCard from '../layout/GlassCard.vue'

interface AgentInfo {
  id: string
  name: string
  type: string
  status: string
  skills: string[]
  metrics: {
    success_rate: number
    avg_latency_ms: number
    total_requests: number
  }
}

const props = defineProps<{
  agent: AgentInfo | null
  active?: boolean
}>()

function statusColor(status: string): string {
  switch (status) {
    case 'HEALTHY': return 'var(--color-success)'
    case 'DEGRADED': return 'var(--color-warning)'
    case 'DOWN': return 'var(--color-error)'
    default: return 'var(--text-tertiary)'
  }
}

function statusLabel(status: string): string {
  switch (status) {
    case 'HEALTHY': return '健康'
    case 'DEGRADED': return '降级'
    case 'DOWN': return '离线'
    default: return status
  }
}

function initial(name: string): string {
  return name.charAt(0).toUpperCase()
}

function formatNumber(n: number): string {
  return n.toLocaleString()
}
</script>

<template>
  <Transition name="slide-in">
    <div v-if="agent" class="detail-panel">
      <GlassCard variant="highlight" padding="lg">
        <!-- Header -->
        <div class="panel-header">
          <div class="avatar" :style="{ background: 'var(--brand-gradient)' }">
            {{ initial(agent.name) }}
          </div>
          <div class="header-info">
            <h3 class="agent-name">{{ agent.name }}</h3>
            <div class="status-row">
              <span class="status-dot" :style="{ background: statusColor(agent.status) }"></span>
              <span class="status-text" :style="{ color: statusColor(agent.status) }">
                {{ statusLabel(agent.status) }}
              </span>
            </div>
          </div>
        </div>

        <!-- Active indicator -->
        <div v-if="active" class="active-indicator">
          <span class="active-pulse"></span>
          <span class="active-text">正在处理任务...</span>
        </div>

        <!-- Type -->
        <div class="type-section">
          <span class="type-badge">{{ agent.type }}</span>
        </div>

        <!-- Skills -->
        <div class="skills-section">
          <h4 class="section-title">能力</h4>
          <div class="skills-list">
            <span v-for="skill in agent.skills" :key="skill" class="skill-tag">
              {{ skill }}
            </span>
          </div>
        </div>

        <!-- Metrics -->
        <div class="metrics-section">
          <h4 class="section-title">性能指标</h4>
          <div class="metrics-grid">
            <div class="metric-card">
              <div class="metric-label">成功率</div>
              <div class="metric-value">{{ (agent.metrics.success_rate * 100).toFixed(1) }}%</div>
              <div class="progress-bar">
                <div
                  class="progress-fill"
                  :style="{ width: (agent.metrics.success_rate * 100) + '%' }"
                ></div>
              </div>
            </div>
            <div class="metric-card">
              <div class="metric-label">平均延迟</div>
              <div class="metric-value">{{ agent.metrics.avg_latency_ms }}<span class="unit">ms</span></div>
            </div>
            <div class="metric-card">
              <div class="metric-label">总请求数</div>
              <div class="metric-value">{{ formatNumber(agent.metrics.total_requests) }}</div>
            </div>
          </div>
        </div>
      </GlassCard>
    </div>
  </Transition>
</template>

<style scoped>
@import "../../styles/design-tokens.css";

.detail-panel {
  width: 320px;
  min-width: 320px;
  animation: slideInRight var(--duration-normal) var(--ease-out);
}

@keyframes slideInRight {
  from {
    transform: translateX(20px);
    opacity: 0;
  }
  to {
    transform: translateX(0);
    opacity: 1;
  }
}

.slide-in-enter-active {
  transition: all var(--duration-normal) var(--ease-out);
}
.slide-in-leave-active {
  transition: all var(--duration-fast) var(--ease-in);
}
.slide-in-enter-from {
  transform: translateX(20px);
  opacity: 0;
}
.slide-in-leave-to {
  transform: translateX(20px);
  opacity: 0;
}

.panel-header {
  display: flex;
  align-items: center;
  gap: var(--space-4);
  margin-bottom: var(--space-5);
}

.avatar {
  width: 52px;
  height: 52px;
  min-width: 52px;
  border-radius: var(--radius-full);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 22px;
  font-weight: 700;
  color: #fff;
  box-shadow: var(--shadow-glow-brand);
}

.header-info {
  flex: 1;
  min-width: 0;
}

.agent-name {
  font-size: 18px;
  font-weight: 700;
  color: var(--text-primary);
  margin: 0 0 var(--space-1) 0;
}

.status-row {
  display: flex;
  align-items: center;
  gap: var(--space-2);
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: var(--radius-full);
  animation: pulse-dot 2s ease-in-out infinite;
}

@keyframes pulse-dot {
  0%, 100% { box-shadow: 0 0 4px currentColor; }
  50% { box-shadow: 0 0 10px currentColor, 0 0 16px currentColor; }
}

.status-text {
  font-size: 13px;
  font-weight: 500;
}

.type-section {
  margin-bottom: var(--space-5);
}

.type-badge {
  display: inline-block;
  font-size: 11px;
  padding: 3px 8px;
  background: rgba(99, 102, 241, 0.12);
  border: 1px solid rgba(99, 102, 241, 0.3);
  border-radius: var(--radius-sm);
  color: var(--brand-primary);
  text-transform: uppercase;
  letter-spacing: 0.5px;
  font-weight: 600;
}

.skills-section {
  margin-bottom: var(--space-5);
}

.section-title {
  font-size: 12px;
  font-weight: 600;
  color: var(--text-tertiary);
  text-transform: uppercase;
  letter-spacing: 1px;
  margin: 0 0 var(--space-3) 0;
}

.skills-list {
  display: flex;
  flex-wrap: wrap;
  gap: var(--space-2);
}

.skill-tag {
  font-size: 12px;
  padding: 4px 10px;
  background: var(--glass-bg);
  backdrop-filter: blur(var(--glass-blur));
  -webkit-backdrop-filter: blur(var(--glass-blur));
  border: 1px solid var(--glass-border);
  border-radius: var(--radius-full);
  color: var(--text-secondary);
  transition: all var(--duration-fast) var(--ease-default);
}

.skill-tag:hover {
  background: var(--glass-bg-hover);
  border-color: var(--border-brand);
  color: var(--text-primary);
}

.metrics-section {
  margin-top: var(--space-5);
}

.metrics-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: var(--space-3);
}

.metric-card {
  padding: var(--space-2);
  background: rgba(255, 255, 255, 0.04);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-md);
}

.metric-label {
  font-size: 11px;
  color: var(--text-tertiary);
  text-transform: uppercase;
  letter-spacing: 0.5px;
  margin-bottom: var(--space-1);
}

.metric-value {
  font-size: 18px;
  font-weight: 700;
  color: var(--text-primary);
  font-family: var(--font-mono);
}

.metric-value .unit {
  font-size: 13px;
  font-weight: 500;
  color: var(--text-tertiary);
  margin-left: 2px;
}

.progress-bar {
  height: 4px;
  background: rgba(255, 255, 255, 0.06);
  border-radius: var(--radius-full);
  margin-top: var(--space-2);
  overflow: hidden;
}

.progress-fill {
  height: 100%;
  background: var(--brand-gradient);
  border-radius: var(--radius-full);
  transition: width var(--duration-slow) var(--ease-out);
}

/* Active indicator */
.active-indicator {
  display: flex;
  align-items: center;
  gap: var(--space-2);
  padding: var(--space-3) var(--space-4);
  margin-bottom: var(--space-4);
  background: rgba(99, 102, 241, 0.08);
  border: 1px solid rgba(99, 102, 241, 0.25);
  border-radius: var(--radius-md);
}

.active-pulse {
  width: 8px;
  height: 8px;
  border-radius: var(--radius-full);
  background: #818cf8;
  animation: active-pulse-anim 1.5s ease-in-out infinite;
  flex-shrink: 0;
}

@keyframes active-pulse-anim {
  0%, 100% { opacity: 1; box-shadow: 0 0 4px #818cf8; }
  50% { opacity: 0.5; box-shadow: 0 0 12px #818cf8, 0 0 20px rgba(129, 140, 248, 0.4); }
}

.active-text {
  font-size: 12px;
  font-weight: 500;
  color: #a5b4fc;
}

/* Reduced motion */
@media (prefers-reduced-motion: reduce) {
  .detail-panel {
    animation: none;
  }

  .status-dot {
    animation: none;
  }

  .active-pulse {
    animation: none;
  }
}

/* Mobile: full-width bottom panel */
@media (max-width: 768px) {
  .detail-panel {
    width: 100%;
    min-width: 100%;
  }

  .metrics-grid {
    grid-template-columns: repeat(3, 1fr);
  }
}
</style>
