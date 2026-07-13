<template>
  <div class="chat-view">
    <div class="chat-main">
      <div class="chat-header">
        <div class="header-brand">
          <div class="brand-icon">
            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <polygon points="12 2 22 8.5 22 15.5 12 22 2 15.5 2 8.5 12 2"/><line x1="12" y1="22" x2="12" y2="15.5"/><polyline points="22 8.5 12 15.5 2 8.5"/>
            </svg>
          </div>
          <h1>NexusAI</h1>
        </div>
        <div class="header-actions">
          <button class="btn-icon" @click="showActivityPanel = !showActivityPanel" :class="{ active: showActivityPanel }" title="Activity Log">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/>
              <line x1="16" y1="13" x2="8" y2="13"/><line x1="16" y1="17" x2="8" y2="17"/><polyline points="10 9 9 9 8 9"/>
            </svg>
          </button>
          <button class="btn-icon" @click="exportMarkdown" title="Export Markdown">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/>
            </svg>
          </button>
          <button class="btn-text" @click="chatStore.newConversation()">New Chat</button>
          <router-link to="/admin" class="btn-text">Admin</router-link>
          <button class="btn-text btn-logout" @click="handleLogout">Logout</button>
        </div>
      </div>

      <div class="chat-messages" ref="messagesRef">
        <div v-if="chatStore.messages.length === 0" class="empty-state">
          <div class="empty-illustration">
            <svg width="64" height="64" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1" opacity="0.2">
              <polygon points="12 2 22 8.5 22 15.5 12 22 2 15.5 2 8.5 12 2"/>
              <line x1="12" y1="22" x2="12" y2="15.5"/>
              <polyline points="22 8.5 12 15.5 2 8.5"/>
              <line x1="8" y1="11" x2="16" y2="11"/>
              <line x1="8" y1="14" x2="14" y2="14"/>
            </svg>
          </div>
          <h2>Ask NexusAI</h2>
          <p>AI auto-routes to the best Agent, supporting multi-agent collaboration for complex tasks</p>
          <div class="quick-prompts">
            <button
              v-for="prompt in quickPrompts"
              :key="prompt"
              class="prompt-chip"
              @click="quickSend(prompt)"
            >{{ prompt }}</button>
          </div>
        </div>

        <MessageBubble
          v-for="msg in chatStore.messages"
          :key="msg.id"
          :message="msg"
          @feedback="handleFeedback"
        />

        <!-- Agent Selector -->
        <AgentSelector
          v-if="showAgentSelector"
          :candidates="agentCandidates"
          :selected-id="selectedAgentId"
          @select="handleAgentSelect"
        />
      </div>

      <div class="chat-input">
        <div class="input-wrapper">
          <textarea
            v-model="inputText"
            :placeholder="chatStore.isStreaming ? 'Agent is responding...' : 'Enter your question...'"
            :disabled="chatStore.isStreaming"
            @keydown.enter.exact.prevent="handleSend"
            @compositionstart="composing = true"
            @compositionend="composing = false"
            rows="1"
            @input="autoResize"
            ref="textareaRef"
          />
          <button
            v-if="chatStore.isStreaming"
            class="btn-stop"
            @click="chatStore.stopStreaming()"
          >
            <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor"><rect x="6" y="4" width="4" height="16"/><rect x="14" y="4" width="4" height="16"/></svg>
            Stop
          </button>
          <button
            v-else
            class="btn-send"
            :disabled="!inputText.trim()"
            @click="handleSend"
          >
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <line x1="22" y1="2" x2="11" y2="13"/><polygon points="22 2 15 22 11 13 2 9 22 2"/>
            </svg>
          </button>
        </div>
      </div>
    </div>

    <!-- Activity Panel Sidebar -->
    <transition name="slide">
      <div v-if="showActivityPanel" class="chat-sidebar">
        <ActivityPanel :entries="activityEntries" />
      </div>
    </transition>
  </div>
</template>

<script setup lang="ts">
import { ref, nextTick, watch } from 'vue'
import { useRouter } from 'vue-router'
import { useChatStore } from '../stores/chat'
import { useAuthStore } from '../stores/auth'
import MessageBubble from '../components/MessageBubble.vue'
import ActivityPanel from '../components/ActivityPanel.vue'
import AgentSelector from '../components/AgentSelector.vue'
import type { AgentDisplayInfo, ActivityEntry } from '../types/proto'

const router = useRouter()
const chatStore = useChatStore()
const authStore = useAuthStore()
const inputText = ref('')
const composing = ref(false)
const messagesRef = ref<HTMLElement>()
const textareaRef = ref<HTMLTextAreaElement>()
const showActivityPanel = ref(false)

// Agent selector state
const showAgentSelector = ref(false)
const agentCandidates = ref<AgentDisplayInfo[]>([])
const selectedAgentId = ref<string>()

// Activity feed
const activityEntries = ref<ActivityEntry[]>([])

const quickPrompts = [
  'Explain microservices architecture',
  'Write a quicksort in Python',
  'Compare REST vs GraphQL',
  'How to design a high-availability system?',
]

function quickSend(text: string) {
  inputText.value = text
  handleSend()
}

function handleSend() {
  if (composing.value) return
  const text = inputText.value.trim()
  if (!text) return
  chatStore.sendQuestion(text)
  inputText.value = ''
  if (textareaRef.value) {
    textareaRef.value.style.height = 'auto'
  }
  // Add activity entry
  addActivity('agent_call', `Sending query: ${text.slice(0, 60)}${text.length > 60 ? '...' : ''}`)
}

function handleFeedback(msgId: string, type: 'like' | 'dislike') {
  chatStore.setFeedback(msgId, type)
  addActivity('complete', type === 'like' ? 'User marked as helpful 👍' : 'User marked as not helpful 👎')
}

function handleAgentSelect(agentId: string) {
  selectedAgentId.value = agentId
  showAgentSelector.value = false
  addActivity('agent_call', `Agent selected: ${agentId}`)
}

function addActivity(type: ActivityEntry['type'], message: string, extra?: Partial<ActivityEntry>) {
  activityEntries.value = [...activityEntries.value, {
    timestamp: Date.now(),
    type,
    message,
    ...extra,
  }]
  // Keep last 50
  if (activityEntries.value.length > 50) {
    activityEntries.value = activityEntries.value.slice(-50)
  }
}

function handleLogout() {
  authStore.logout()
  router.push('/login')
}

function autoResize() {
  if (textareaRef.value) {
    textareaRef.value.style.height = 'auto'
    textareaRef.value.style.height = Math.min(textareaRef.value.scrollHeight, 150) + 'px'
  }
}

function exportMarkdown() {
  const msgs = chatStore.messages
  if (msgs.length === 0) return

  let md = '# NexusAI Conversation Log\n\n'
  md += `> Exported: ${new Date().toLocaleString()}\n`
  md += `> Conversation ID: ${chatStore.contextId}\n\n---\n\n`

  for (const msg of msgs) {
    const role = msg.role === 'user' ? '👤 **User**' : `🤖 **${msg.agentName || 'Agent'}**`
    md += `### ${role}\n\n`
    if (msg.traceInfo) {
      md += `> Route ${msg.traceInfo.route_time_ms}ms → ${msg.traceInfo.agent_name} ${msg.traceInfo.agent_time_ms}ms (total ${msg.traceInfo.total_time_ms}ms)\n\n`
    }
    md += `${msg.content}\n\n---\n\n`
  }

  const blob = new Blob([md], { type: 'text/markdown' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `nexusai-${chatStore.contextId}.md`
  a.click()
  URL.revokeObjectURL(url)

  addActivity('complete', 'Conversation exported as Markdown')
}

// Watch for streaming events to populate activity feed
watch(
  () => chatStore.messages[chatStore.messages.length - 1]?.executionPlan,
  (plan) => {
    if (plan) {
      addActivity('thinking', `Execution plan: ${plan.tasks.length} subtask(s)`)
      plan.tasks.forEach(t => {
        addActivity('thinking', `Task [${t.id}]: ${t.description}`)
      })
    }
  },
)

// Auto-scroll
watch(
  () => chatStore.messages.length,
  () => nextTick(() => scrollToBottom()),
)

watch(
  () => chatStore.messages[chatStore.messages.length - 1]?.content,
  () => nextTick(() => scrollToBottom()),
)

function scrollToBottom() {
  if (messagesRef.value) {
    messagesRef.value.scrollTop = messagesRef.value.scrollHeight
  }
}
</script>

<style scoped>
.chat-view {
  display: flex;
  height: 100vh;
  background: #fff;
}

.chat-main {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.chat-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 24px;
  border-bottom: 1px solid #e5e7eb;
  background: #fff;
}

.header-brand {
  display: flex;
  align-items: center;
  gap: 10px;
}

.brand-icon {
  display: flex;
  align-items: center;
  color: #6366f1;
}

.header-brand h1 {
  font-size: 18px;
  font-weight: 700;
  margin: 0;
  background: linear-gradient(135deg, #6366f1, #3b82f6);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 6px;
}

.btn-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 36px;
  height: 36px;
  border: 1px solid transparent;
  border-radius: 8px;
  background: transparent;
  color: #6b7280;
  cursor: pointer;
  transition: all 0.15s;
}

.btn-icon:hover {
  background: #f3f4f6;
  color: #374151;
}

.btn-icon.active {
  background: #eff6ff;
  color: #3b82f6;
  border-color: #bfdbfe;
}

.btn-text {
  padding: 6px 12px;
  border-radius: 6px;
  font-size: 13px;
  cursor: pointer;
  text-decoration: none;
  border: 1px solid #e5e7eb;
  background: #fff;
  color: #374151;
  transition: all 0.15s;
  font-weight: 500;
}

.btn-text:hover {
  background: #f3f4f6;
  border-color: #d1d5db;
}

.btn-logout {
  color: #9ca3af;
  border-color: transparent;
}

.btn-logout:hover {
  color: #dc2626;
}

.chat-messages {
  flex: 1;
  overflow-y: auto;
  padding: 24px;
  scroll-behavior: smooth;
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100%;
  text-align: center;
}

.empty-illustration {
  margin-bottom: 16px;
}

.empty-state h2 {
  font-size: 20px;
  font-weight: 600;
  color: #1f2937;
  margin: 0 0 8px;
}

.empty-state p {
  color: #9ca3af;
  font-size: 14px;
  margin: 0 0 24px;
}

.quick-prompts {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  justify-content: center;
  max-width: 500px;
}

.prompt-chip {
  padding: 8px 14px;
  border: 1px solid #e5e7eb;
  border-radius: 20px;
  background: #fff;
  color: #4b5563;
  font-size: 13px;
  cursor: pointer;
  transition: all 0.15s;
}

.prompt-chip:hover {
  background: #f0f9ff;
  border-color: #7dd3fc;
  color: #0369a1;
}

.chat-input {
  padding: 16px 24px;
  border-top: 1px solid #e5e7eb;
  background: #fff;
}

.input-wrapper {
  display: flex;
  align-items: flex-end;
  gap: 10px;
  border: 1px solid #d1d5db;
  border-radius: 14px;
  padding: 8px 14px;
  transition: border-color 0.15s, box-shadow 0.15s;
  background: #fff;
}

.input-wrapper:focus-within {
  border-color: #6366f1;
  box-shadow: 0 0 0 3px rgba(99, 102, 241, 0.1);
}

textarea {
  flex: 1;
  border: none;
  outline: none;
  resize: none;
  font-size: 15px;
  line-height: 1.5;
  font-family: inherit;
  min-height: 24px;
  background: transparent;
}

textarea::placeholder {
  color: #9ca3af;
}

.btn-send, .btn-stop {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 8px 16px;
  border-radius: 10px;
  font-size: 14px;
  font-weight: 600;
  border: none;
  cursor: pointer;
  white-space: nowrap;
  transition: all 0.15s;
}

.btn-send {
  background: #6366f1;
  color: #fff;
}

.btn-send:hover:not(:disabled) {
  background: #4f46e5;
  transform: scale(1.02);
}

.btn-send:disabled {
  background: #c7d2fe;
  cursor: not-allowed;
}

.btn-stop {
  background: #ef4444;
  color: #fff;
}

.btn-stop:hover {
  background: #dc2626;
}

/* Sidebar */
.chat-sidebar {
  width: 320px;
  border-left: 1px solid #e5e7eb;
  padding: 16px;
  overflow-y: auto;
  background: #fafafa;
}

.slide-enter-active,
.slide-leave-active {
  transition: all 0.25s ease;
}

.slide-enter-from,
.slide-leave-to {
  width: 0;
  opacity: 0;
  padding: 0;
}
</style>
