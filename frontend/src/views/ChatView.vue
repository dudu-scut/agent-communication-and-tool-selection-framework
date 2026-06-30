<template>
  <div class="chat-view">
    <div class="chat-header">
      <h1>NexusAI</h1>
      <div class="header-actions">
        <span v-if="chatStore.lastAgentName" class="last-agent">
          {{ chatStore.lastAgentName }}
        </span>
        <button class="btn-new" @click="chatStore.newConversation()">
          新对话
        </button>
        <router-link to="/admin" class="btn-admin">管理</router-link>
      </div>
    </div>

    <div class="chat-messages" ref="messagesRef">
      <div v-if="chatStore.messages.length === 0" class="empty-state">
        <p>向 NexusAI 提问，它会自动路由到最合适的 Agent</p>
      </div>

      <MessageBubble
        v-for="msg in chatStore.messages"
        :key="msg.id"
        :message="msg"
      />
    </div>

    <div class="chat-input">
      <div class="input-wrapper">
        <textarea
          v-model="inputText"
          :placeholder="chatStore.isStreaming ? 'Agent 正在回答...' : '输入你的问题...'"
          :disabled="chatStore.isStreaming"
          @keydown.enter.exact.prevent="handleSend"
          rows="1"
          @input="autoResize"
          ref="textareaRef"
        />
        <button
          v-if="chatStore.isStreaming"
          class="btn-stop"
          @click="chatStore.stopStreaming()"
        >
          停止
        </button>
        <button
          v-else
          class="btn-send"
          :disabled="!inputText.trim()"
          @click="handleSend"
        >
          发送
        </button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, nextTick, watch } from 'vue'
import { useChatStore } from '../stores/chat'
import MessageBubble from '../components/MessageBubble.vue'

const chatStore = useChatStore()
const inputText = ref('')
const messagesRef = ref<HTMLElement>()
const textareaRef = ref<HTMLTextAreaElement>()

function handleSend() {
  const text = inputText.value.trim()
  if (!text) return
  chatStore.sendQuestion(text)
  inputText.value = ''
  if (textareaRef.value) {
    textareaRef.value.style.height = 'auto'
  }
}

function autoResize() {
  if (textareaRef.value) {
    textareaRef.value.style.height = 'auto'
    textareaRef.value.style.height = Math.min(textareaRef.value.scrollHeight, 150) + 'px'
  }
}

// 自动滚动到底部
watch(
  () => chatStore.messages.length,
  () => {
    nextTick(() => scrollToBottom())
  },
)

// 监听流式内容变化时也滚动
watch(
  () => chatStore.messages[chatStore.messages.length - 1]?.content,
  () => {
    nextTick(() => scrollToBottom())
  },
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
  flex-direction: column;
  height: 100vh;
  max-width: 800px;
  margin: 0 auto;
  background: #fff;
}

.chat-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 16px 24px;
  border-bottom: 1px solid #e5e7eb;
}

.chat-header h1 {
  font-size: 20px;
  font-weight: 600;
  margin: 0;
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 12px;
}

.last-agent {
  font-size: 13px;
  color: #6b7280;
}

.btn-new, .btn-admin {
  padding: 6px 14px;
  border-radius: 6px;
  font-size: 13px;
  cursor: pointer;
  text-decoration: none;
  border: 1px solid #d1d5db;
  background: #fff;
  color: #374151;
  transition: background 0.15s;
}

.btn-new:hover, .btn-admin:hover {
  background: #f3f4f6;
}

.chat-messages {
  flex: 1;
  overflow-y: auto;
  padding: 24px;
}

.empty-state {
  display: flex;
  align-items: center;
  justify-content: center;
  height: 100%;
  color: #9ca3af;
  font-size: 15px;
}

.chat-input {
  padding: 16px 24px;
  border-top: 1px solid #e5e7eb;
}

.input-wrapper {
  display: flex;
  align-items: flex-end;
  gap: 10px;
  border: 1px solid #d1d5db;
  border-radius: 12px;
  padding: 8px 12px;
  transition: border-color 0.15s;
}

.input-wrapper:focus-within {
  border-color: #3b82f6;
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
}

textarea::placeholder {
  color: #9ca3af;
}

.btn-send, .btn-stop {
  padding: 6px 16px;
  border-radius: 8px;
  font-size: 14px;
  font-weight: 500;
  border: none;
  cursor: pointer;
  white-space: nowrap;
  transition: background 0.15s;
}

.btn-send {
  background: #3b82f6;
  color: #fff;
}

.btn-send:hover:not(:disabled) {
  background: #2563eb;
}

.btn-send:disabled {
  background: #93c5fd;
  cursor: not-allowed;
}

.btn-stop {
  background: #ef4444;
  color: #fff;
}

.btn-stop:hover {
  background: #dc2626;
}
</style>
