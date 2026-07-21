/**
 * TypeScript type definitions matching protobuf messages in proto/
 * Hand-written as a replacement for protoc generation; keep in sync with proto definitions
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
  agent_metrics?: AgentMetrics
  cacheable?: boolean
  deployment_stage?: string
  a2a_version?: string
}

// === ai_query.proto ===

export interface AgentPreference {
  preferred_skills: string[]
  preferred_agents: string[]
  allow_fallback: boolean
}

export interface SystemContext {
  user_id: string
  user_memory: string
  conversation_history: string
  cross_agent_summary: string
}

export interface AIQueryRequest {
  request_id: string
  question: string
  context_id: string
  history_length: number
  timeout_seconds: number
  metadata: Record<string, string>
  preference?: AgentPreference
  user_id?: string
  system_context?: SystemContext
}

export interface AIQueryResponse {
  request_id: string
  status: Status
  answer: string
  agent_id: string
  agent_name: string
  task_id: string
  context_id: string
  processing_time_ms: number | string
  memory_hints?: Record<string, string>
  artifacts?: Artifact[]
}

export interface Artifact {
  name: string
  mime_type: string
  content: string
}

export interface AIStreamEvent {
  event_id: string
  event_type: 'partial' | 'status' | 'complete' | 'error' | 'plan' | 'subtask_start' | 'subtask_complete' | 'activity_json' | 'trace_summary'
  content: string
  task_state: string
  context_id: string
  timestamp: number
  trace_id?: string
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
  required_skills?: string[]
}

export interface FindAgentsResponse {
  status: Status
  agents: ServiceInfo[]
  total_count: number
}

// === Agent Metrics ===

export interface AgentMetrics {
  agent_id: string
  success_rate: number        // 0~1
  avg_latency_ms: number
  p95_latency_ms?: number
  total_requests: number
  approval_rate?: number      // 好评率

  // 以下字段后端可能不返回，标记为optional
  estimated_token_low?: number
  estimated_token_high?: number
  active_requests?: number
  circuit_breaker_trips?: number
  health_status?: string
  last_heartbeat?: string
  cpu_percent?: number
  memory_mb?: number
}

export interface GetAgentMetricsRequest {
  agent_id: string
}

export interface GetAgentMetricsResponse {
  status: Status
  metrics: AgentMetrics
}

// === Budget ===

export interface BudgetInfo {
  user_id: string
  daily_limit: number
  daily_used: number
  monthly_limit: number
  monthly_used: number
  remaining: number
  reset_at: number
}

// === Multi-Agent Execution Plan ===

export interface SubTaskInfo {
  id: string
  description: string
  skill: string
  depends_on: string[]
  status: 'pending' | 'running' | 'completed' | 'failed'
  result?: string
  assigned_agent_id?: string
}

export interface ExecutionPlan {
  original_query: string
  tasks: SubTaskInfo[]
}

// === Activity Feed ===

export interface ActivityEntry {
  timestamp: number
  type: 'thinking' | 'tool_call' | 'agent_call' | 'complete' | 'error'
  message: string
  agent_name?: string
  tool_name?: string
  duration_ms?: number
  detail?: string
}

// === Trace Info ===

export interface TraceInfo {
  trace_id: string
  route_time_ms: number
  agent_time_ms: number
  total_time_ms: number
  agent_name: string
  agent_id: string
  skill: string
}

// === user.proto (auth) ===

export interface RegisterRequest {
  username: string
  password: string
  display_name: string
}

export interface RegisterResponse {
  status: Status
  user_id: string
  username: string
}

export interface LoginRequest {
  username: string
  password: string
}

export interface LoginResponse {
  status: Status
  user_id: string
  username: string
  token: string
  expires_at: number
}

// === Sharing & Templates ===

export interface ShareInfo {
  share_id: string
  context_id: string
  owner_user_id: string
  mode: 'READONLY' | 'COMMENT'
  created_at: number
  expires_at?: number
}

export interface TemplateInfo {
  id: string
  name: string
  description: string
  category: string
  usage_count: number
  rating: number
  author: string
  dag_structure: ExecutionPlan
}

// === Cron / Scheduled Tasks ===

export interface ScheduledTask {
  id: string
  name: string
  cron_expr: string
  query_template: string
  enabled: boolean
  last_run_at?: number
  next_run_at?: number
  execution_count: number
  last_result?: string
}

// === Canary Deployment ===

export interface CanaryConfig {
  agent_id_stable: string
  agent_id_canary: string
  traffic_split_pct: number  // % to canary
  stable_metrics: AgentMetrics
  canary_metrics: AgentMetrics
  status: 'running' | 'promoting' | 'rolling_back' | 'completed'
}

// === Frontend internal types ===

export interface ChatMessage {
  id: string
  role: 'user' | 'agent'
  content: string
  agentName?: string
  agentId?: string
  processingTimeMs?: number
  streaming?: boolean
  error?: string
  executionPlan?: ExecutionPlan
  activityFeed?: ActivityEntry[]
  traceInfo?: TraceInfo
  feedbackGiven?: 'like' | 'dislike' | null
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
  metrics?: AgentMetrics
}

// === observability.proto ===

export interface TokenUsageRecord {
  trace_id: string
  user_id: string
  context_id: string
  agent_id: string
  component: string
  prompt_tokens: number
  completion_tokens: number
  cost_usd: number
  latency_ms: number
  created_at: string
}

export interface TraceSpan {
  trace_id: string
  span_id: string
  parent_span_id: string
  component: string
  start_time: string
  end_time: string
  duration_ms: number
  status: string
  error_message: string
  metadata_json: string
}

export interface CostRecord {
  user_id: string
  date: string           // YYYY-MM-DD
  total_cost_usd: number
  total_prompt_tokens: number
  total_completion_tokens: number
  total_requests: number
}

export interface GetTraceDetailResponse {
  status: Status
  spans: TraceSpan[]
  trace_summary: string
}

export interface GetCostReportResponse {
  status: Status
  records: CostRecord[]
  total_cost_usd: number
}
