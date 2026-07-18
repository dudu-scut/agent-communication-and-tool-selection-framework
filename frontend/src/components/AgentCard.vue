<template>
  <div class="agent-card">
    <div class="card-header">
      <div class="agent-name">{{ agent.name }}</div>
      <span class="status-dot" :class="{ healthy: agent.healthy }"></span>
      <span class="status-text">{{ agent.healthy ? 'Healthy' : 'Unhealthy' }}</span>
    </div>

    <div class="card-body">
      <div class="info-row">
        <span class="label">ID</span>
        <span class="value">{{ agent.id }}</span>
      </div>
      <div class="info-row">
        <span class="label">Address</span>
        <span class="value">{{ agent.host }}:{{ agent.port }}</span>
      </div>
      <div v-if="agent.version" class="info-row">
        <span class="label">Version</span>
        <span class="value">{{ agent.version }}</span>
      </div>

      <div v-if="agent.skills.length > 0" class="skills">
        <span class="label">Skills</span>
        <div class="skill-tags">
          <span v-for="skill in agent.skills" :key="skill" class="skill-tag">
            {{ skill }}
          </span>
        </div>
      </div>

      <div v-if="agent.tags.length > 0" class="tags">
        <span class="label">Tags</span>
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
@import "../styles/design-tokens.css";

.agent-card {
  border-radius: var(--radius-lg);
  overflow: hidden;
  transition: all var(--duration-normal) var(--ease-default);
  background: var(--glass-bg);
  backdrop-filter: blur(var(--glass-blur));
  -webkit-backdrop-filter: blur(var(--glass-blur));
  border: 1px solid var(--glass-border);
}

.agent-card:hover {
  transform: translateY(-2px);
  box-shadow: var(--shadow-md);
  border-color: var(--border-strong);
}

.card-header {
  display: flex;
  align-items: center;
  gap: var(--space-2);
  padding: var(--space-3) var(--space-4);
  background: var(--bg-elevated);
  border-bottom: 1px solid var(--border-subtle);
}

.agent-name {
  font-weight: 600;
  font-size: 15px;
  flex: 1;
  color: var(--text-primary);
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--text-tertiary);
  transition: all var(--duration-normal) var(--ease-default);
}

.status-dot.healthy {
  background: var(--color-success);
  box-shadow: var(--shadow-glow-success);
}

.status-text {
  font-size: 12px;
  color: var(--text-tertiary);
}

.card-body {
  padding: var(--space-3) var(--space-4);
}

.info-row {
  display: flex;
  gap: var(--space-3);
  margin-bottom: var(--space-2);
  font-size: 13px;
}

.label {
  color: var(--text-muted);
  min-width: 48px;
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.value {
  color: var(--text-secondary);
  font-family: var(--font-mono);
  font-size: 12px;
}

.skills, .tags {
  margin-top: var(--space-3);
}

.skills .label, .tags .label {
  display: block;
  margin-bottom: var(--space-2);
  font-size: 12px;
}

.skill-tags, .tag-list {
  display: flex;
  flex-wrap: wrap;
  gap: var(--space-2);
}

.skill-tag {
  padding: 2px 10px;
  border-radius: var(--radius-full);
  font-size: 12px;
  background: rgba(139, 92, 246, 0.12);
  color: var(--brand-secondary);
  border: 1px solid var(--border-brand);
  transition: all var(--duration-fast) var(--ease-default);
}

.skill-tag:hover {
  background: rgba(139, 92, 246, 0.2);
  transform: translateY(-1px);
}

.tag {
  padding: 2px 8px;
  border-radius: var(--radius-sm);
  font-size: 11px;
  background: var(--bg-elevated);
  color: var(--text-tertiary);
  border: 1px solid var(--border-subtle);
  transition: all var(--duration-fast) var(--ease-default);
}

.tag:hover {
  background: var(--glass-bg-hover);
  color: var(--text-secondary);
}
</style>
