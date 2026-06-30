import { defineStore } from 'pinia'
import { ref } from 'vue'
import { getAgents } from '../services/grpc-client'
import type { AgentDisplayInfo, ServiceInfo } from '../types/proto'

export const useAgentsStore = defineStore('agents', () => {
  const agents = ref<AgentDisplayInfo[]>([])
  const loading = ref(false)
  const lastFetched = ref<number>(0)
  const error = ref<string>('')

  async function fetchAgents() {
    loading.value = true
    error.value = ''

    try {
      const resp = await getAgents()
      agents.value = resp.agents.map(toAgentDisplayInfo)
      lastFetched.value = Date.now()
    } catch (err: unknown) {
      error.value = err instanceof Error ? err.message : 'Failed to fetch agents'
    } finally {
      loading.value = false
    }
  }

  function toAgentDisplayInfo(info: ServiceInfo): AgentDisplayInfo {
    let cardSkills: string[] = []
    try {
      if (info.agent_card) {
        const card = JSON.parse(info.agent_card)
        if (card.skills) {
          cardSkills = card.skills.map(
            (s: { name: string }) => s.name,
          )
        }
      }
    } catch {
      // agent_card JSON 解析失败，fallback 到 skills 字段
    }

    return {
      id: info.service_name,
      name: info.service_name,
      host: info.host,
      port: info.port,
      version: info.version,
      tags: info.tags || [],
      skills: cardSkills.length > 0 ? cardSkills : info.skills || [],
      healthy: true, // 能从 GetAgents 返回的默认是健康的
    }
  }

  // 自动轮询：每 15 秒刷新一次
  let pollTimer: ReturnType<typeof setInterval> | null = null

  function startPolling(intervalMs = 15000) {
    stopPolling()
    fetchAgents()
    pollTimer = setInterval(fetchAgents, intervalMs)
  }

  function stopPolling() {
    if (pollTimer) {
      clearInterval(pollTimer)
      pollTimer = null
    }
  }

  return {
    agents,
    loading,
    lastFetched,
    error,
    fetchAgents,
    startPolling,
    stopPolling,
  }
})
