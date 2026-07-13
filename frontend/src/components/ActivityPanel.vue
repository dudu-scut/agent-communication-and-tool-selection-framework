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
.activity-panel {
  background: #fff;
  border: 1px solid #e5e7eb;
  border-radius: 12px;
  overflow: hidden;
  transition: all 0.3s ease;
}

.activity-panel.collapsed {
  border-color: #f3f4f6;
}

.panel-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 16px;
  cursor: pointer;
  user-select: none;
  transition: background 0.15s;
}

.panel-header:hover {
  background: #fafafa;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 10px;
}

.header-title {
  font-size: 14px;
  font-weight: 600;
  color: #1f2937;
}

.entry-count {
  padding: 1px 7px;
  border-radius: 10px;
  background: #eff6ff;
  color: #3b82f6;
  font-size: 11px;
  font-weight: 600;
}

.pulse-icon {
  color: #3b82f6;
  animation: pulse 2s ease-in-out infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}

.chevron {
  color: #9ca3af;
  transition: transform 0.25s ease;
}

.chevron.rotated {
  transform: rotate(180deg);
}

.panel-body {
  max-height: 420px;
  overflow-y: auto;
  border-top: 1px solid #f3f4f6;
}

.empty-feed {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 32px 16px;
  text-align: center;
}

.empty-feed p {
  font-size: 13px;
  color: #9ca3af;
  margin-top: 12px;
  line-height: 1.5;
  max-width: 200px;
}

.feed-list {
  padding: 8px;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.feed-entry {
  display: flex;
  align-items: flex-start;
  gap: 10px;
  padding: 8px 10px;
  border-radius: 8px;
  transition: background 0.15s;
}

.feed-entry:hover {
  background: #f9fafb;
}

.feed-entry.entry-thinking {
  border-left: 3px solid #f59e0b;
}

.feed-entry.entry-tool_call {
  border-left: 3px solid #6366f1;
}

.feed-entry.entry-agent_call {
  border-left: 3px solid #3b82f6;
}

.feed-entry.entry-complete {
  border-left: 3px solid #22c55e;
}

.feed-entry.entry-error {
  border-left: 3px solid #ef4444;
  background: #fef2f2;
}

.entry-icon {
  font-size: 16px;
  flex-shrink: 0;
  margin-top: 1px;
}

.entry-body {
  flex: 1;
  min-width: 0;
}

.entry-message {
  font-size: 13px;
  color: #374151;
  line-height: 1.4;
}

.entry-meta {
  display: flex;
  gap: 8px;
  margin-top: 3px;
  flex-wrap: wrap;
}

.entry-agent {
  font-size: 11px;
  color: #3b82f6;
  font-weight: 500;
}

.entry-tool {
  font-size: 11px;
  padding: 0 5px;
  border-radius: 4px;
  background: #f3f4f6;
  color: #6b7280;
  font-family: monospace;
}

.entry-duration {
  font-size: 11px;
  color: #9ca3af;
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
