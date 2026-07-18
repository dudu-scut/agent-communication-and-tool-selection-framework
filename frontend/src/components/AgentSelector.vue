<template>
  <div class="agent-selector" v-if="candidates.length > 0">
    <div class="selector-header">
      <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
        <path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/>
        <path d="M23 21v-2a4 4 0 0 0-3-3.87"/><path d="M16 3.13a4 4 0 0 1 0 7.75"/>
      </svg>
      <span>{{ candidates.length }} agent(s) matched</span>
    </div>

    <div class="candidate-list">
      <div
        v-for="agent in sortedCandidates"
        :key="agent.id"
        class="candidate-card"
        :class="{ selected: selectedId === agent.id }"
        @click="$emit('select', agent.id)"
      >
        <div class="candidate-header">
          <span class="candidate-name">{{ agent.name }}</span>
          <span
            class="health-dot"
            :class="agent.healthy ? 'healthy' : 'unhealthy'"
            :title="agent.healthy ? 'Online' : 'Offline'"
          ></span>
        </div>

        <div class="candidate-skills">
          <span v-for="skill in agent.skills" :key="skill" class="skill-tag">{{ skill }}</span>
        </div>

        <div v-if="agent.metrics" class="candidate-metrics">
          <div class="metric">
            <span class="metric-label">Success</span>
            <span class="metric-value" :class="agent.metrics.success_rate >= 0.9 ? 'good' : 'warn'">
              {{ (agent.metrics.success_rate * 100).toFixed(0) }}%
            </span>
          </div>
          <div class="metric">
            <span class="metric-label">Latency</span>
            <span class="metric-value">{{ agent.metrics.avg_latency_ms }}ms</span>
          </div>
          <div class="metric">
            <span class="metric-label">Approval</span>
            <span class="metric-value">{{ ((agent.metrics.approval_rate ?? 0) * 100).toFixed(0) }}%</span>
          </div>
        </div>

        <div v-if="selectedId === agent.id" class="selected-check">
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3">
            <polyline points="20 6 9 17 4 12"/>
          </svg>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { AgentDisplayInfo } from '../types/proto'

const props = defineProps<{
  candidates: AgentDisplayInfo[]
  selectedId?: string
}>()

defineEmits<{ select: [agentId: string] }>()

const sortedCandidates = computed(() => {
  return [...props.candidates].sort((a, b) => {
    const aScore = a.metrics ? a.metrics.success_rate * 0.4 + (a.metrics.approval_rate ?? 0) * 0.3 + (1 - a.metrics.avg_latency_ms / 10000) * 0.3 : 0
    const bScore = b.metrics ? b.metrics.success_rate * 0.4 + (b.metrics.approval_rate ?? 0) * 0.3 + (1 - b.metrics.avg_latency_ms / 10000) * 0.3 : 0
    return bScore - aScore
  })
})
</script>

<style scoped>
@import "../styles/design-tokens.css";

.agent-selector {
  margin: var(--space-3) 0;
  border: 1px solid var(--glass-border);
  border-radius: var(--radius-md);
  overflow: hidden;
  background: var(--glass-bg);
  backdrop-filter: blur(var(--glass-blur));
}

.selector-header {
  display: flex;
  align-items: center;
  gap: var(--space-2);
  padding: var(--space-3) var(--space-4);
  background: var(--bg-elevated);
  border-bottom: 1px solid var(--border-subtle);
  font-size: 13px;
  font-weight: 600;
  color: var(--text-secondary);
}

.selector-header svg {
  color: var(--brand-primary);
}

.candidate-list {
  display: flex;
  flex-direction: column;
}

.candidate-card {
  position: relative;
  padding: var(--space-3) var(--space-4);
  border-bottom: 1px solid var(--border-subtle);
  cursor: pointer;
  transition: all var(--duration-fast) var(--ease-default);
}

.candidate-card:last-child {
  border-bottom: none;
}

.candidate-card:hover {
  background: var(--glass-bg-hover);
}

.candidate-card.selected {
  background: rgba(16, 185, 129, 0.08);
  border-left: 2px solid var(--color-success);
}

.candidate-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: var(--space-1);
}

.candidate-name {
  font-size: 14px;
  font-weight: 600;
  color: var(--text-primary);
}

.health-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}

.health-dot.healthy {
  background: var(--color-success);
  box-shadow: var(--shadow-glow-success);
}

.health-dot.unhealthy {
  background: var(--color-error);
  box-shadow: var(--shadow-glow-error);
}

.candidate-skills {
  display: flex;
  gap: var(--space-1);
  flex-wrap: wrap;
  margin-bottom: var(--space-2);
}

.skill-tag {
  padding: 1px 6px;
  border-radius: var(--radius-sm);
  background: var(--bg-elevated);
  color: var(--text-tertiary);
  font-size: 11px;
  border: 1px solid var(--border-subtle);
}

.candidate-metrics {
  display: flex;
  gap: var(--space-4);
}

.metric {
  display: flex;
  flex-direction: column;
  gap: 1px;
}

.metric-label {
  font-size: 10px;
  color: var(--text-muted);
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.metric-value {
  font-size: 13px;
  font-weight: 600;
  color: var(--text-secondary);
  font-family: var(--font-mono);
}

.metric-value.good {
  color: var(--color-success);
}

.metric-value.warn {
  color: var(--color-warning);
}

.selected-check {
  position: absolute;
  top: 50%;
  right: var(--space-4);
  transform: translateY(-50%);
  color: var(--color-success);
}
</style>
