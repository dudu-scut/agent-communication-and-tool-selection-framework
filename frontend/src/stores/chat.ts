import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { queryStream } from '../services/grpc-client'
import type { ChatMessage, AIStreamEvent } from '../types/proto'

export const useChatStore = defineStore('chat', () => {
  const messages = ref<ChatMessage[]>([])
  const isStreaming = ref(false)
  const contextId = ref(generateContextId())
  const abortController = ref<AbortController | null>(null)

  const lastAgentName = computed(() => {
    for (let i = messages.value.length - 1; i >= 0; i--) {
      if (messages.value[i].role === 'agent' && messages.value[i].agentName) {
        return messages.value[i].agentName
      }
    }
    return ''
  })

  function sendQuestion(text: string) {
    if (isStreaming.value || !text.trim()) return

    // 添加用户消息
    messages.value.push({
      id: crypto.randomUUID(),
      role: 'user',
      content: text,
      timestamp: Date.now(),
    })

    // 创建空的 Agent 消息占位
    const agentMsg: ChatMessage = {
      id: crypto.randomUUID(),
      role: 'agent',
      content: '',
      streaming: true,
      timestamp: Date.now(),
    }
    messages.value.push(agentMsg)
    isStreaming.value = true

    const ac = new AbortController()
    abortController.value = ac

    queryStream(
      text,
      (event: AIStreamEvent) => handleStreamEvent(event, agentMsg),
      contextId.value,
      ac.signal,
    )
  }

  function handleStreamEvent(event: AIStreamEvent, msg: ChatMessage) {
    switch (event.event_type) {
      case 'partial':
        msg.content += event.content
        break

      case 'status':
        // 从 status 事件中提取 Agent 名称
        if (event.content) {
          msg.agentName = event.content
        }
        break

      case 'complete':
        msg.streaming = false
        msg.processingTimeMs = event.timestamp
          ? Date.now() - msg.timestamp
          : undefined
        isStreaming.value = false
        abortController.value = null
        break

      case 'error':
        msg.error = event.content || 'Unknown error'
        msg.streaming = false
        isStreaming.value = false
        abortController.value = null
        break
    }
  }

  function stopStreaming() {
    if (abortController.value) {
      abortController.value.abort()
      abortController.value = null
    }
    isStreaming.value = false
    // 标记最后一条消息为停止
    const last = messages.value[messages.value.length - 1]
    if (last?.streaming) {
      last.streaming = false
      last.content += '\n[已停止]'
    }
  }

  function newConversation() {
    stopStreaming()
    messages.value = []
    contextId.value = generateContextId()
  }

  return {
    messages,
    isStreaming,
    contextId,
    lastAgentName,
    sendQuestion,
    stopStreaming,
    newConversation,
  }
})

function generateContextId(): string {
  return 'ctx-' + crypto.randomUUID().slice(0, 8)
}
