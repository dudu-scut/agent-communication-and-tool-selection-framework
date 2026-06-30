<template>
  <div class="agent-card">
    <div class="card-header">
      <div class="agent-name">{{ agent.name }}</div>
      <span class="status-dot" :class="{ healthy: agent.healthy }"></span>
      <span class="status-text">{{ agent.healthy ? '健康' : '不健康' }}</span>
    </div>

    <div class="card-body">
      <div class="info-row">
        <span class="label">ID</span>
        <span class="value">{{ agent.id }}</span>
      </div>
      <div class="info-row">
        <span class="label">地址</span>
        <span class="value">{{ agent.host }}:{{ agent.port }}</span>
      </div>
      <div v-if="agent.version" class="info-row">
        <span class="label">版本</span>
        <span class="value">{{ agent.version }}</span>
      </div>

      <div v-if="agent.skills.length > 0" class="skills">
        <span class="label">技能</span>
        <div class="skill-tags">
          <span v-for="skill in agent.skills" :key="skill" class="skill-tag">
            {{ skill }}
          </span>
        </div>
      </div>

      <div v-if="agent.tags.length > 0" class="tags">
        <span class="label">标签</span>
        <div class="tag-list">
          <span v-for="tag in agent.tags" :key="tag" class="tag">
            {{ tag }}
          </span>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { AgentDisplayInfo } from '../types/proto'

defineProps<{
  agent: AgentDisplayInfo
}>()
</script>

<style scoped>
.agent-card {
  border: 1px solid #e5e7eb;
  border-radius: 12px;
  overflow: hidden;
  transition: box-shadow 0.15s;
}

.agent-card:hover {
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.06);
}

.card-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 14px 16px;
  background: #f9fafb;
  border-bottom: 1px solid #e5e7eb;
}

.agent-name {
  font-weight: 600;
  font-size: 15px;
  flex: 1;
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #d1d5db;
}

.status-dot.healthy {
  background: #22c55e;
}

.status-text {
  font-size: 12px;
  color: #6b7280;
}

.card-body {
  padding: 14px 16px;
}

.info-row {
  display: flex;
  gap: 12px;
  margin-bottom: 8px;
  font-size: 13px;
}

.label {
  color: #9ca3af;
  min-width: 36px;
}

.value {
  color: #374151;
  font-family: 'SF Mono', 'Cascadia Code', monospace;
  font-size: 12px;
}

.skills, .tags {
  margin-top: 12px;
}

.skills .label, .tags .label {
  display: block;
  margin-bottom: 6px;
  font-size: 12px;
}

.skill-tags, .tag-list {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.skill-tag {
  padding: 2px 10px;
  border-radius: 12px;
  font-size: 12px;
  background: #ede9fe;
  color: #6d28d9;
}

.tag {
  padding: 2px 8px;
  border-radius: 4px;
  font-size: 11px;
  background: #f3f4f6;
  color: #6b7280;
}
</style>
