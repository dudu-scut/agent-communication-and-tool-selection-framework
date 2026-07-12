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

    <!-- Trace summary -->
    <div v-if="message.traceInfo" class="trace-summary">
      <span class="trace-badge" :title="'Trace ID: ' + message.traceInfo.trace_id">
        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>
        路由 {{ message.traceInfo.route_time_ms }}ms
      </span>
      <span class="trace-arrow">→</span>
      <span class="trace-agent">{{ message.traceInfo.agent_name }}</span>
      <span class="trace-arrow">→</span>
      <span class="trace-badge">
        <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>
        Agent {{ message.traceInfo.agent_time_ms }}ms
      </span>
      <span class="trace-total">{{ message.traceInfo.total_time_ms }}ms 总计</span>
    </div>

    <!-- Feedback buttons -->
    <div v-if="message.role === 'agent' && !message.streaming && !message.error" class="feedback-row">
      <button
        class="feedback-btn like-btn"
        :class="{ active: message.feedbackGiven === 'like' }"
        @click="$emit('feedback', message.id, 'like')"
        title="有帮助"
      >
        <svg width="16" height="16" viewBox="0 0 24 24" :fill="message.feedbackGiven === 'like' ? 'currentColor' : 'none'" stroke="currentColor" stroke-width="2">
          <path d="M14 9V5a3 3 0 0 0-3-3l-4 9v11h11.28a2 2 0 0 0 2-1.7l1.38-9a2 2 0 0 0-2-2.3H14z"/>
        </svg>
      </button>
      <button
        class="feedback-btn dislike-btn"
        :class="{ active: message.feedbackGiven === 'dislike' }"
        @click="$emit('feedback', message.id, 'dislike')"
        title="无帮助"
      >
        <svg width="16" height="16" viewBox="0 0 24 24" :fill="message.feedbackGiven === 'dislike' ? 'currentColor' : 'none'" stroke="currentColor" stroke-width="2">
          <path d="M10 15v4a3 3 0 0 0 3 3l4-9V2H5.72a2 2 0 0 0-2 1.7l-1.38 9a2 2 0 0 0 2 2.3H10z"/>
        </svg>
      </button>
      <button class="feedback-btn copy-btn" @click="copyContent" title="复制回答">
        <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <rect x="9" y="9" width="13" height="13" rx="2" ry="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/>
        </svg>
      </button>
      <span v-if="copied" class="copied-hint">已复制</span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import type { ChatMessage } from '../types/proto'
import AgentBadge from './AgentBadge.vue'
import StreamingText from './StreamingText.vue'
import ExecutionPlan from './ExecutionPlan.vue'

defineProps<{ message: ChatMessage }>()
defineEmits<{ feedback: [messageId: string, type: 'like' | 'dislike'] }>()

const copied = ref(false)

function formatTime(ms: number): string {
  if (ms < 1000) return `${ms}ms`
  return `${(ms / 1000).toFixed(1)}s`
}

async function copyContent() {
  const el = (document.querySelector('.message-content') as HTMLElement)
  if (el) {
    await navigator.clipboard.writeText(el.innerText)
    copied.value = true
    setTimeout(() => { copied.value = false }, 2000)
  }
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

/* Trace Summary */
.trace-summary {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-top: 8px;
  padding: 6px 12px;
  background: linear-gradient(135deg, #f8fafc 0%, #f1f5f9 100%);
  border: 1px solid #e2e8f0;
  border-radius: 8px;
  font-size: 12px;
  color: #64748b;
  flex-wrap: wrap;
}

.trace-badge {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 2px 8px;
  background: #fff;
  border: 1px solid #e2e8f0;
  border-radius: 12px;
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 11px;
  color: #475569;
}

.trace-arrow {
  color: #94a3b8;
  font-size: 10px;
}

.trace-agent {
  font-weight: 600;
  color: #3b82f6;
}

.trace-total {
  margin-left: auto;
  font-weight: 600;
  color: #0f172a;
}

/* Feedback Row */
.feedback-row {
  display: flex;
  align-items: center;
  gap: 4px;
  margin-top: 8px;
  padding-left: 2px;
}

.feedback-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 32px;
  height: 32px;
  border: 1px solid transparent;
  border-radius: 8px;
  background: transparent;
  color: #94a3b8;
  cursor: pointer;
  transition: all 0.15s ease;
}

.feedback-btn:hover {
  background: #f1f5f9;
  color: #475569;
  border-color: #e2e8f0;
}

.feedback-btn.like-btn.active {
  background: #ecfdf5;
  color: #16a34a;
  border-color: #86efac;
}

.feedback-btn.dislike-btn.active {
  background: #fef2f2;
  color: #dc2626;
  border-color: #fecaca;
}

.copied-hint {
  font-size: 11px;
  color: #16a34a;
  margin-left: 4px;
  animation: fadeIn 0.2s ease;
}

@keyframes fadeIn {
  from { opacity: 0; transform: translateY(-4px); }
  to { opacity: 1; transform: translateY(0); }
}
</style>
