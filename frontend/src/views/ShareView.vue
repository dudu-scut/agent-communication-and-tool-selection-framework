<template>
  <div class="share-view">
    <div v-if="loading" class="loading-state">
      <div class="spinner"></div>
      <p>加载共享会话...</p>
    </div>

    <div v-else-if="error" class="error-state">
      <div class="error-icon">
        <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="10"/><line x1="15" y1="9" x2="9" y2="15"/><line x1="9" y1="9" x2="15" y2="15"/></svg>
      </div>
      <h2>{{ error }}</h2>
      <p>此共享链接可能已过期或不存在</p>
    </div>

    <div v-else class="shared-content">
      <div class="share-header">
        <div class="share-info">
          <h1>
            <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="12 2 22 8.5 22 15.5 12 22 2 15.5 2 8.5 12 2"/><line x1="12" y1="22" x2="12" y2="15.5"/></svg>
            NexusAI 共享会话
          </h1>
          <div class="share-meta">
            <span>由 {{ shareData.owner }} 分享</span>
            <span class="dot">·</span>
            <span>{{ shareData.mode === 'READONLY' ? '只读' : '可评论' }}</span>
            <span class="dot">·</span>
            <span>{{ shareData.messageCount }} 条消息</span>
          </div>
        </div>
        <div class="share-badge">只读</div>
      </div>

      <div class="messages-list">
        <div v-for="msg in shareData.messages" :key="msg.id" class="shared-message" :class="msg.role">
          <div class="msg-role">
            {{ msg.role === 'user' ? '👤 用户' : '🤖 ' + (msg.agentName || 'Agent') }}
          </div>
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
const error = ref('')

interface SharedMessage {
  id: string
  role: 'user' | 'agent'
  content: string
  agentName?: string
}

const shareData = ref({
  owner: 'anonymous',
  mode: 'READONLY',
  messageCount: 0,
  messages: [] as SharedMessage[],
})

onMounted(async () => {
  const shareId = route.params.shareId as string
  if (!shareId) {
    error.value = '无效的共享链接'
    loading.value = false
    return
  }

  // Simulate loading shared session
  await new Promise(r => setTimeout(r, 1000))

  shareData.value = {
    owner: 'demo-user',
    mode: 'READONLY',
    messageCount: 4,
    messages: [
      { id: '1', role: 'user', content: '帮我解释一下什么是分布式系统？' },
      { id: '2', role: 'agent', agentName: 'General Assistant', content: '分布式系统是由多台计算机组成的系统，这些计算机通过网络通信和协调，共同完成一个任务。\n\n核心特征：\n1. **并发性**：多个节点同时工作\n2. **缺乏全局时钟**：无法精确同步所有节点\n3. **独立故障**：部分节点故障不应影响整体\n\n常见的分布式系统包括：分布式数据库、微服务架构、区块链等。' },
      { id: '3', role: 'user', content: '能举个具体的例子吗？' },
      { id: '4', role: 'agent', agentName: 'General Assistant', content: '以电商系统为例：\n\n- **商品服务**：管理商品信息\n- **订单服务**：处理下单流程\n- **支付服务**：处理支付\n- **库存服务**：管理库存\n\n这些服务独立部署、独立扩展，通过 API 互相调用。这就是典型的微服务（分布式系统的一种）。' },
    ],
  }
  loading.value = false
})
</script>

<style scoped>
.share-view {
  min-height: 100vh; background: #f8fafc; display: flex; flex-direction: column;
}

.loading-state, .error-state {
  display: flex; flex-direction: column; align-items: center; justify-content: center;
  flex: 1; gap: 16px;
}

.spinner {
  width: 36px; height: 36px; border: 3px solid #e5e7eb;
  border-top-color: #6366f1; border-radius: 50%;
  animation: spin 0.8s linear infinite;
}

@keyframes spin { to { transform: rotate(360deg); } }

.error-icon { color: #fca5a5; }
.error-state h2 { font-size: 18px; color: #374151; margin: 0; }
.error-state p { color: #9ca3af; font-size: 14px; }

.shared-content {
  max-width: 760px; margin: 0 auto; width: 100%; padding: 24px;
}

.share-header {
  display: flex; align-items: flex-start; justify-content: space-between;
  background: #fff; border: 1px solid #e5e7eb; border-radius: 14px;
  padding: 20px 24px; margin-bottom: 20px;
}

.share-info h1 {
  display: flex; align-items: center; gap: 10px;
  font-size: 18px; font-weight: 700; margin: 0 0 8px; color: #1f2937;
}

.share-info h1 svg { color: #6366f1; }

.share-meta { display: flex; align-items: center; gap: 6px; font-size: 13px; color: #9ca3af; }
.dot { font-weight: 600; }

.share-badge {
  padding: 4px 12px; border-radius: 12px; background: #eff6ff;
  color: #3b82f6; font-size: 12px; font-weight: 600;
}

.messages-list { display: flex; flex-direction: column; gap: 16px; }

.shared-message {
  background: #fff; border: 1px solid #e5e7eb; border-radius: 12px; padding: 16px 20px;
}

.shared-message.user { border-left: 3px solid #3b82f6; }

.shared-message.agent { border-left: 3px solid #22c55e; }

.msg-role { font-size: 12px; font-weight: 600; color: #9ca3af; margin-bottom: 8px; }

.msg-content { font-size: 14px; line-height: 1.7; white-space: pre-wrap; color: #1f2937; }
</style>
