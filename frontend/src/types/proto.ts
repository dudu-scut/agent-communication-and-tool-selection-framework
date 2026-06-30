/**
 * TypeScript 类型定义，对应 proto/ 下的 protobuf 消息
 * 手写替代 protoc 生成，保持与 proto 定义同步
 */

// === common.proto ===

export interface Status {
  code: number
  message: string
  details: string
}

export interface ServiceInfo {
  service_name: string
  version: string
  host: string
  port: number
  tags: string[]
  metadata: Record<string, string>
  skills: string[]
  agent_card: string // JSON string of AgentCard
}

// === ai_query.proto ===

export interface AgentPreference {
  preferred_skills: string[]
  preferred_agents: string[]
  allow_fallback: boolean
}

export interface AIQueryRequest {
  request_id: string
  question: string
  context_id: string
  history_length: number
  timeout_seconds: number
  metadata: Record<string, string>
  preference?: AgentPreference
}

export interface AIQueryResponse {
  request_id: string
  status: Status
  answer: string
  agent_id: string
  agent_name: string
  task_id: string
  context_id: string
  processing_time_ms: number
}

export interface AIStreamEvent {
  event_id: string
  event_type: 'partial' | 'status' | 'complete' | 'error'
  content: string
  task_state: string
  context_id: string
  timestamp: number
}

// === agent_service.proto ===

export interface GetAgentsRequest {
  filter: string
  limit: number
  offset: number
}

export interface GetAgentsResponse {
  status: Status
  agents: ServiceInfo[]
  total_count: number
}

export interface FindAgentsRequest {
  tag: string
  skill: string
  keyword: string
  limit: number
}

export interface FindAgentsResponse {
  status: Status
  agents: ServiceInfo[]
  total_count: number
}

// === 前端内部类型 ===

export interface ChatMessage {
  id: string
  role: 'user' | 'agent'
  content: string
  agentName?: string
  agentId?: string
  processingTimeMs?: number
  streaming?: boolean
  error?: string
  timestamp: number
}

export interface AgentDisplayInfo {
  id: string
  name: string
  host: string
  port: number
  version: string
  tags: string[]
  skills: string[]
  healthy: boolean
  lastHeartbeat?: number
}
