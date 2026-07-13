<template>
  <div class="share-view">
    <div v-if="loading" class="share-status">
      <div class="spinner"></div>
      <p>Loading shared session...</p>
    </div>

    <div v-else-if="error" class="share-status">
      <div class="error-icon">
        <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="10"/><line x1="15" y1="9" x2="9" y2="15"/><line x1="9" y1="9" x2="15" y2="15"/></svg>
      </div>
      <p>This shared link may have expired or does not exist</p>
      <router-link to="/" class="go-home">Back to NexusAI</router-link>
    </div>

    <div v-else class="share-content">
      <div class="share-header">
        <div>
          <h1>
            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="12 2 22 8.5 22 15.5 12 22 2 15.5 2 8.5 12 2"/></svg>
            NexusAI Shared Session
          </h1>
          <div class="share-meta">
            <span>Shared by {{ shareData.owner }}</span>
            <span class="dot">·</span>
            <span>{{ shareData.mode === 'READONLY' ? 'Read-only' : 'Commentable' }}</span>
            <span class="dot">·</span>
            <span>{{ shareData.messageCount }} messages</span>
          </div>
        </div>
        <div class="share-badge">Read-only</div>
      </div>

      <div class="share-messages">
        <div v-for="msg in shareData.messages" :key="msg.id" class="shared-message" :class="msg.role">
          <div class="msg-role">{{ msg.role === 'user' ? '👤 User' : '🤖 ' + (msg.agentName || 'Agent') }}</div>
          <div class="msg-content">{{ msg.content }}</div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRoute } from 'vue-router'

const route = useRoute()
const loading = ref(true)
const error = ref(false)

interface SharedMessage {
  id: string
  role: 'user' | 'agent'
  content: string
  agentName?: string
}

interface SharedSession {
  owner: string
  mode: string
  messageCount: number
  messages: SharedMessage[]
}

const shareData = ref<SharedSession>({
  owner: '',
  mode: 'READONLY',
  messageCount: 0,
  messages: [],
})

onMounted(async () => {
  const shareId = route.params.shareId as string
  if (!shareId) {
    error.value = true
    loading.value = false
    return
  }

  // Simulate loading a shared session
  await new Promise(r => setTimeout(r, 800))

  // Mock data
  shareData.value = {
    owner: 'demo-user',
    mode: 'READONLY',
    messageCount: 4,
    messages: [
      { id: '1', role: 'user', content: 'Explain what a distributed system is.' },
      { id: '2', role: 'agent', agentName: 'General Assistant', content: 'A distributed system is a collection of independent computers that appear to users as a single coherent system. These computers communicate and coordinate their actions by passing messages over a network.\n\nKey characteristics:\n1. **Concurrency**: Multiple nodes work simultaneously\n2. **No global clock**: Cannot precisely synchronize all nodes\n3. **Independent failures**: A partial node failure should not affect the whole\n\nCommon examples include distributed databases, microservice architectures, and blockchain networks.' },
      { id: '3', role: 'user', content: 'Can you give a concrete example?' },
      { id: '4', role: 'agent', agentName: 'General Assistant', content: 'Take an e-commerce platform as an example:\n\n- **Product Service**: Manages product information\n- **Order Service**: Processes order creation flow\n- **Payment Service**: Handles payment processing\n- **Inventory Service**: Manages stock levels\n\nThese services are deployed independently, scale independently, and communicate via APIs. This is a classic microservice architecture (a form of distributed system).' },
    ],
  }
  loading.value = false
})
</script>

<style scoped>
.share-view {
  min-height: 100vh;
  background: #f8fafc;
}

.share-status {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  color: #6b7280;
  gap: 16px;
}

.spinner {
  width: 36px; height: 36px;
  border: 3px solid #e5e7eb;
  border-top-color: #6366f1;
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
}

@keyframes spin { to { transform: rotate(360deg); } }

.error-icon { color: #ef4444; }

.go-home {
  margin-top: 8px;
  padding: 8px 18px;
  border-radius: 8px;
  background: #6366f1;
  color: #fff;
  text-decoration: none;
  font-size: 14px;
  font-weight: 500;
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
  border-bottom: 1px solid #e5e7eb;
}

.share-header h1 {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 20px;
  font-weight: 700;
  color: #1f2937;
  margin: 0 0 8px;
}

.share-meta {
  font-size: 13px;
  color: #9ca3af;
  display: flex;
  gap: 8px;
}

.dot { color: #d1d5db; }

.share-badge {
  padding: 4px 12px;
  border-radius: 12px;
  background: #fef3c7;
  color: #92400e;
  font-size: 12px;
  font-weight: 600;
}

.share-messages {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.shared-message.user .msg-content {
  background: #3b82f6;
  color: #fff;
  border-radius: 14px 14px 4px 14px;
  margin-left: 60px;
}

.shared-message.agent .msg-content {
  background: #f3f4f6;
  color: #1f2937;
  border-radius: 14px 14px 14px 4px;
  margin-right: 60px;
}

.msg-role {
  font-size: 12px;
  color: #9ca3af;
  margin-bottom: 4px;
  padding: 0 4px;
}

.msg-content {
  padding: 12px 16px;
  font-size: 14px;
  line-height: 1.6;
  white-space: pre-wrap;
  word-break: break-word;
}
</style>
