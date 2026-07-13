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
            <span class="metric-value">{{ (agent.metrics.approval_rate * 100).toFixed(0) }}%</span>
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
    const aScore = a.metrics ? a.metrics.success_rate * 0.4 + a.metrics.approval_rate * 0.3 + (1 - a.metrics.avg_latency_ms / 10000) * 0.3 : 0
    const bScore = b.metrics ? b.metrics.success_rate * 0.4 + b.metrics.approval_rate * 0.3 + (1 - b.metrics.avg_latency_ms / 10000) * 0.3 : 0
    return bScore - aScore
  })
})
</script>

<style scoped>
.agent-selector {
  margin: 12px 0;
  border: 1px solid #e5e7eb;
  border-radius: 12px;
  overflow: hidden;
  background: #fff;
}

.selector-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 14px;
  background: #f0f9ff;
  border-bottom: 1px solid #bae6fd;
  font-size: 13px;
  font-weight: 600;
  color: #0369a1;
}

.candidate-list {
  display: flex;
  flex-direction: column;
}

.candidate-card {
  position: relative;
  padding: 12px 14px;
  border-bottom: 1px solid #f3f4f6;
  cursor: pointer;
  transition: all 0.15s ease;
}

.candidate-card:last-child {
  border-bottom: none;
}

.candidate-card:hover {
  background: #f8fafc;
}

.candidate-card.selected {
  background: #f0fdf4;
  border-color: #bbf7d0;
}

.candidate-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 6px;
}

.candidate-name {
  font-size: 14px;
  font-weight: 600;
  color: #1f2937;
}

.health-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}

.health-dot.healthy {
  background: #22c55e;
  box-shadow: 0 0 4px rgba(34, 197, 94, 0.4);
}

.health-dot.unhealthy {
  background: #ef4444;
}

.candidate-skills {
  display: flex;
  gap: 4px;
  flex-wrap: wrap;
  margin-bottom: 8px;
}

.skill-tag {
  padding: 1px 6px;
  border-radius: 4px;
  background: #f3f4f6;
  color: #6b7280;
  font-size: 11px;
}

.candidate-metrics {
  display: flex;
  gap: 16px;
}

.metric {
  display: flex;
  flex-direction: column;
  gap: 1px;
}

.metric-label {
  font-size: 10px;
  color: #9ca3af;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.metric-value {
  font-size: 13px;
  font-weight: 600;
  color: #374151;
  font-family: 'SF Mono', 'Fira Code', monospace;
}

.metric-value.good {
  color: #16a34a;
}

.metric-value.warn {
  color: #f59e0b;
}

.selected-check {
  position: absolute;
  top: 50%;
  right: 14px;
  transform: translateY(-50%);
  color: #16a34a;
}
</style>
