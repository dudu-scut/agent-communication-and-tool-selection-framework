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
    // 获取响应式代理对象——闭包中必须使用 proxy 而非原始对象，
    // 否则 Vue 无法检测到属性变更
    const reactiveMsg = messages.value[messages.value.length - 1]
    isStreaming.value = true

    const ac = new AbortController()
    abortController.value = ac

    queryStream(
      text,
      (event: AIStreamEvent) => handleStreamEvent(event, reactiveMsg),
      contextId.value,
      ac.signal,
    ).finally(() => {
      // Safety net: if server disconnects without a complete/error event
      if (reactiveMsg.streaming) {
        reactiveMsg.streaming = false
        reactiveMsg.content += '\n[连接断开]'
      }
      isStreaming.value = false
      abortController.value = null
    })
  }

  function handleStreamEvent(event: AIStreamEvent, msg: ChatMessage) {
    switch (event.event_type) {
      case 'partial':
        // 清除规划阶段的占位文本，替换为真实内容
        if (msg.content === '正在分析请求...') {
          msg.content = ''
        }
        msg.content += event.content
        break

      case 'status':
        // "thinking" during planning phase, or agent name from A2A
        if (event.task_state === 'planning') {
          if (!msg.content) {
            msg.content = '正在分析请求...'
          }
        } else if (event.content && event.content !== 'thinking') {
          msg.agentName = event.content
        }
        break

      case 'plan':
        // 解析多Agent执行计划 JSON
        try {
          const plan = JSON.parse(event.content)
          msg.executionPlan = {
            original_query: plan.original_query,
            tasks: (plan.tasks || []).map((t: any) => ({
              id: t.id,
              description: t.description,
              skill: t.skill,
              depends_on: t.depends_on || [],
              status: 'pending' as const,
            })),
          }
        } catch {
          // malformed plan JSON, ignore
        }
        break

      case 'subtask_start':
        if (msg.executionPlan) {
          const task = msg.executionPlan.tasks.find(t => t.id === event.task_state)
          if (task) task.status = 'running'
        }
        break

      case 'subtask_complete':
        if (msg.executionPlan) {
          const task = msg.executionPlan.tasks.find(t => t.id === event.task_state)
          if (task) {
            task.status = event.content?.startsWith('FAILED:') ? 'failed' : 'completed'
            task.result = event.content
          }
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
    // Find the last streaming agent message (not just the last in array)
    for (let i = messages.value.length - 1; i >= 0; i--) {
      const msg = messages.value[i]
      if (msg.role === 'agent' && msg.streaming) {
        msg.streaming = false
        msg.content += '\n[已停止]'
        break
      }
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
