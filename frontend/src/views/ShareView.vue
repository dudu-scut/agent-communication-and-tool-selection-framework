<template>
  <div class="share-view">
    <div class="share-content">
      <div class="share-header">
        <div>
          <h1>
            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="12 2 22 8.5 22 15.5 12 22 2 15.5 2 8.5 12 2"/></svg>
            NexusAI Shared Session
          </h1>
          <div class="share-meta">
            <span v-if="state === 'result'">{{ title }}</span>
            <span v-if="state === 'result' && sharedAt">Shared at {{ sharedAt }}</span>
          </div>
        </div>
      </div>

      <!-- Loading -->
      <GlassCard v-if="state === 'loading'" variant="highlight" padding="lg">
        <div class="state-block">
          <div class="spinner" />
          <p>Loading shared conversation…</p>
        </div>
      </GlassCard>

      <!-- Error: real backend error semantics -->
      <GlassCard v-else-if="state === 'error'" variant="highlight" padding="lg">
        <div class="state-block error">
          <h3>Unable to load shared conversation</h3>
          <p class="error-message">{{ errorMessage }}</p>
          <button class="retry-btn" @click="load">Try again</button>
        </div>
      </GlassCard>

      <!-- Empty -->
      <GlassCard v-else-if="state === 'empty'" variant="highlight" padding="lg">
        <EmptyState
          icon="mdi:message-outline"
          title="No messages"
          description="This shared conversation contains no messages."
        />
      </GlassCard>

      <!-- Result -->
      <div v-else class="messages">
        <GlassCard
          v-for="msg in messages"
          :key="msg.sequence_no.toString()"
          :variant="msg.role === 'user' ? 'default' : 'highlight'"
          padding="md"
          class="message-card"
        >
          <div class="message-role">{{ msg.role === 'user' ? 'User' : 'Agent' }}</div>
          <div class="message-content">{{ msg.content }}</div>
        </GlassCard>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRoute } from 'vue-router'
import GlassCard from '../components/layout/GlassCard.vue'
import EmptyState from '../components/feedback/EmptyState.vue'
import { readSharedConversation } from '../services/grpc-client'
import type { SharedMessage } from '../types/proto'

const route = useRoute()

const state = ref<'loading' | 'error' | 'empty' | 'result'>('loading')
const errorMessage = ref('')
const title = ref('')
const sharedAt = ref('')
const messages = ref<SharedMessage[]>([])

async function load() {
  state.value = 'loading'
  errorMessage.value = ''
  // The :shareId route parameter IS the raw bearer token.
  const token = String(route.params.shareId ?? '')
  if (!token) {
    state.value = 'error'
    errorMessage.value = 'Share link not found'
    return
  }
  try {
    const resp = await readSharedConversation(token)
    const list = resp.messages ?? []
    title.value = resp.title || 'Shared conversation'
    sharedAt.value = resp.shared_at || ''
    messages.value = list
    state.value = list.length > 0 ? 'result' : 'empty'
  } catch (e) {
    state.value = 'error'
    errorMessage.value = e instanceof Error ? e.message : String(e)
  }
}

onMounted(load)
</script>

<style scoped>
@import "../styles/design-tokens.css";

.share-view {
  min-height: 100vh;
  background: var(--bg-primary);
}

.share-content {
  max-width: 800px;
  margin: 0 auto;
  padding: 32px 24px;
}

.share-header {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  margin-bottom: 32px;
  padding-bottom: 20px;
  border-bottom: 1px solid var(--border-default);
}

.share-header h1 {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 20px;
  font-weight: 700;
  color: var(--text-primary);
  margin: 0 0 8px;
}

.share-meta {
  font-size: 13px;
  color: var(--text-tertiary);
  display: flex;
  gap: 12px;
}

.state-block {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  padding: 24px 0;
  color: var(--text-secondary);
}

.state-block.error h3 { color: var(--color-error); margin: 0; }
.error-message { color: var(--text-secondary); margin: 0; word-break: break-word; }

.retry-btn {
  padding: 8px 20px;
  border-radius: var(--radius-md);
  border: 1px solid var(--border-default);
  background: var(--bg-elevated);
  color: var(--text-primary);
  cursor: pointer;
}
.retry-btn:hover { background: var(--glass-bg-hover); }

.spinner {
  width: 28px;
  height: 28px;
  border: 3px solid var(--border-default);
  border-top-color: var(--color-accent, #6c8cff);
  border-radius: 50%;
  animation: spin 0.9s linear infinite;
}
@keyframes spin { to { transform: rotate(360deg); } }

.messages {
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.message-card .message-role {
  font-size: 12px;
  font-weight: 600;
  color: var(--text-tertiary);
  margin-bottom: 6px;
}

.message-card .message-content {
  font-size: 14px;
  color: var(--text-primary);
  white-space: pre-wrap;
  word-break: break-word;
}
</style>
