<template>
  <div class="activity-panel" :class="{ collapsed: !expanded }">
    <div class="panel-header" @click="expanded = !expanded">
      <div class="header-left">
        <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="pulse-icon">
          <circle cx="12" cy="12" r="10"/><path d="M12 6v6l4 2"/>
        </svg>
        <span class="header-title">Activity Log</span>
        <span v-if="entries.length > 0 && expanded" class="entry-count">{{ entries.length }}</span>
      </div>
      <svg
        width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"
        class="chevron" :class="{ rotated: expanded }"
      >
        <polyline points="15 18 9 12 15 6"/>
      </svg>
    </div>

    <div v-if="expanded" class="panel-body">
      <div v-if="entries.length === 0" class="empty-feed">
        <div class="empty-icon">
          <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" opacity="0.3">
            <polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/>
          </svg>
        </div>
        <p>Agent workflow steps appear here in real-time after sending a message</p>
      </div>

      <TransitionGroup name="feed" tag="div" class="feed-list">
        <div
          v-for="entry in entries"
          :key="entry.timestamp + entry.message"
          class="feed-entry"
          :class="'entry-' + entry.type"
        >
          <div class="entry-icon">
            <template v-if="entry.type === 'thinking'">💭</template>
            <template v-else-if="entry.type === 'tool_call'">🔧</template>
            <template v-else-if="entry.type === 'agent_call'">🤖</template>
            <template v-else-if="entry.type === 'complete'">✅</template>
            <template v-else-if="entry.type === 'error'">❌</template>
          </div>
          <div class="entry-body">
            <div class="entry-message">{{ entry.message }}</div>
            <div class="entry-meta">
              <span v-if="entry.agent_name" class="entry-agent">{{ entry.agent_name }}</span>
              <span v-if="entry.tool_name" class="entry-tool">{{ entry.tool_name }}</span>
              <span v-if="entry.duration_ms" class="entry-duration">{{ entry.duration_ms }}ms</span>
            </div>
          </div>
        </div>
      </TransitionGroup>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import type { ActivityEntry } from '../types/proto'

defineProps<{
  entries: ActivityEntry[]
}>()

const expanded = ref(true)
</script>

<style scoped>
@import "../styles/design-tokens.css";

.activity-panel {
  background: var(--glass-bg);
  backdrop-filter: blur(var(--glass-blur));
  border: 1px solid var(--glass-border);
  border-radius: var(--radius-md);
  overflow: hidden;
  transition: all var(--duration-normal) var(--ease-default);
}

.activity-panel.collapsed {
  border-color: var(--border-subtle);
}

.panel-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: var(--space-3) var(--space-4);
  cursor: pointer;
  user-select: none;
  transition: background var(--duration-fast) var(--ease-default);
}

.panel-header:hover {
  background: var(--glass-bg-hover);
}

.header-left {
  display: flex;
  align-items: center;
  gap: var(--space-2);
}

.header-title {
  font-size: 14px;
  font-weight: 600;
  color: var(--text-primary);
}

.entry-count {
  padding: 1px 7px;
  border-radius: var(--radius-full);
  background: rgba(59, 130, 246, 0.15);
  color: var(--color-info);
  font-size: 11px;
  font-weight: 600;
}

.pulse-icon {
  color: var(--color-info);
  animation: pulse 2s ease-in-out infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}

.chevron {
  color: var(--text-tertiary);
  transition: transform var(--duration-normal) var(--ease-default);
}

.chevron.rotated {
  transform: rotate(180deg);
}

.panel-body {
  max-height: 420px;
  overflow-y: auto;
  border-top: 1px solid var(--border-subtle);
}

.empty-feed {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: var(--space-8) var(--space-4);
  text-align: center;
}

.empty-feed p {
  font-size: 13px;
  color: var(--text-tertiary);
  margin-top: var(--space-3);
  line-height: 1.5;
  max-width: 200px;
}

.feed-list {
  padding: var(--space-2);
  display: flex;
  flex-direction: column;
  gap: 0;
  position: relative;
}

/* Timeline connector */
.feed-list::before {
  content: '';
  position: absolute;
  left: 23px;
  top: var(--space-2);
  bottom: var(--space-2);
  width: 1px;
  background: linear-gradient(to bottom, transparent, var(--border-subtle) 10%, var(--border-subtle) 90%, transparent);
}

.feed-entry {
  display: flex;
  align-items: flex-start;
  gap: var(--space-3);
  padding: var(--space-2) var(--space-3);
  border-radius: var(--radius-sm);
  transition: all var(--duration-fast) var(--ease-default);
  position: relative;
}

.feed-entry + .feed-entry {
  margin-top: 2px;
}

.feed-entry:hover {
  background: var(--glass-bg-hover);
}

.feed-entry.entry-thinking {
  border-left: 2px solid var(--color-warning);
}

.feed-entry.entry-tool_call {
  border-left: 2px solid var(--brand-primary);
}

.feed-entry.entry-agent_call {
  border-left: 2px solid var(--color-info);
}

.feed-entry.entry-complete {
  border-left: 2px solid var(--color-success);
}

.feed-entry.entry-error {
  border-left: 2px solid var(--color-error);
  background: rgba(239, 68, 68, 0.06);
}

.entry-icon {
  font-size: 14px;
  flex-shrink: 0;
  margin-top: 1px;
  width: 28px;
  height: 28px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: var(--radius-sm);
  background: var(--bg-elevated);
  border: 1px solid var(--border-subtle);
  position: relative;
  z-index: 1;
}

.entry-body {
  flex: 1;
  min-width: 0;
}

.entry-message {
  font-size: 13px;
  color: var(--text-secondary);
  line-height: 1.4;
}

.entry-meta {
  display: flex;
  gap: var(--space-2);
  margin-top: 3px;
  flex-wrap: wrap;
}

.entry-agent {
  font-size: 11px;
  color: var(--color-info);
  font-weight: 500;
}

.entry-tool {
  font-size: 11px;
  padding: 0 5px;
  border-radius: var(--radius-sm);
  background: var(--bg-elevated);
  color: var(--text-tertiary);
  font-family: var(--font-mono);
}

.entry-duration {
  font-size: 11px;
  color: var(--text-tertiary);
}

/* Feed transition animations */
.feed-enter-active {
  transition: all 0.3s ease-out;
}

.feed-enter-from {
  opacity: 0;
  transform: translateX(-12px);
}

.feed-leave-active {
  transition: all 0.2s ease-in;
}

.feed-leave-to {
  opacity: 0;
  transform: translateX(12px);
}
</style>
