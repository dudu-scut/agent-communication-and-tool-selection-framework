<template>
  <div class="message" :class="message.role">
    <div v-if="message.role === 'agent'" class="message-meta">
      <AgentBadge v-if="message.agentName" :name="message.agentName" />
      <span v-if="message.processingTimeMs" class="time">
        {{ formatTime(message.processingTimeMs) }}
      </span>
    </div>

    <div class="message-content" :class="{ 'has-error': message.error }">
      <ExecutionPlan v-if="message.executionPlan" :plan="message.executionPlan" />
      <template v-if="message.error">
        <span class="error-icon">!</span>
        {{ message.error }}
      </template>
      <template v-else-if="message.streaming">
        <StreamingText :text="message.content" />
      </template>
      <template v-else>
        {{ message.content }}
      </template>
    </div>
  </div>
</template>

<script setup lang="ts">
import type { ChatMessage } from '../types/proto'
import AgentBadge from './AgentBadge.vue'
import StreamingText from './StreamingText.vue'
import ExecutionPlan from './ExecutionPlan.vue'

defineProps<{
  message: ChatMessage
}>()

function formatTime(ms: number): string {
  if (ms < 1000) return `${ms}ms`
  return `${(ms / 1000).toFixed(1)}s`
}
</script>

<style scoped>
.message {
  margin-bottom: 20px;
  max-width: 85%;
}

.message.user {
  margin-left: auto;
}

.message.agent {
  margin-right: auto;
}

.message-meta {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 6px;
}

.time {
  font-size: 12px;
  color: #9ca3af;
}

.message-content {
  padding: 12px 16px;
  border-radius: 16px;
  font-size: 15px;
  line-height: 1.6;
  white-space: pre-wrap;
  word-break: break-word;
}

.message.user .message-content {
  background: #3b82f6;
  color: #fff;
  border-bottom-right-radius: 4px;
}

.message.agent .message-content {
  background: #f3f4f6;
  color: #1f2937;
  border-bottom-left-radius: 4px;
}

.message-content.has-error {
  background: #fef2f2;
  color: #dc2626;
}

.error-icon {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 18px;
  height: 18px;
  border-radius: 50%;
  background: #dc2626;
  color: #fff;
  font-size: 12px;
  font-weight: 700;
  margin-right: 6px;
}
</style>
