/**
 * gRPC-Web client wrapper
 *
 * Communicates with grpcwebproxy via HTTP/1.1, which converts to gRPC/2 and forwards to NexusAI.
 * Currently uses hand-written TypeScript types + fetch; can switch to protoc-generated stubs later.
 */

import type {
  AIQueryRequest,
  AIQueryResponse,
  AIStreamEvent,
  AgentMetrics,
  GetAgentMetricsResponse,
  GetAgentsResponse,
  FindAgentsRequest,
  FindAgentsResponse,
  RegisterRequest,
  RegisterResponse,
  LoginRequest,
  LoginResponse,
  GetTraceDetailResponse,
  GetCostReportResponse,
} from '../types/proto'

const BASE_URL = import.meta.env.VITE_API_BASE || ''

// AIQueryService path prefix
const AI_QUERY = '/agent_communication.AIQueryService'
const AGENT_COMM = '/agent_communication.AgentCommunicationService'
const USER_AUTH = '/agent_communication.auth.UserService'
const OBSERVABILITY = '/agent_communication.ObservabilityService'

// Auth token accessor (set by auth store)
let _getAuthToken: (() => string | null) | null = null
export function setAuthTokenGetter(getter: () => string | null) {
  _getAuthToken = getter
}

// Unauthorized callback (set by auth store to trigger logout on 401)
let _onUnauthorized: (() => void) | null = null
export function setOnUnauthorized(cb: () => void) { _onUnauthorized = cb }

/**
 * Unary RPC call (JSON serialized, forwarded via grpcwebproxy)
 */
async function unaryCall<TReq, TRes>(
  servicePath: string,
  method: string,
  request: TReq,
): Promise<TRes> {
  const url = `${BASE_URL}${servicePath}/${method}`

  const headers: Record<string, string> = { 'Content-Type': 'application/json' }
  const token = _getAuthToken?.()
  if (token) {
    headers['Authorization'] = `Bearer ${token}`
  }

  const resp = await fetch(url, {
    method: 'POST',
    headers,
    body: JSON.stringify(request),
  })

  if (!resp.ok) {
    if (resp.status === 401) {
      _onUnauthorized?.()
    }
    throw new Error(`RPC ${method} failed: ${resp.status} ${resp.statusText}`)
  }

  return resp.json() as Promise<TRes>
}

// ============================================================================
// AIQueryService
// ============================================================================

/** Synchronous query */
export async function query(
  question: string,
  contextId?: string,
): Promise<AIQueryResponse> {
  const req: AIQueryRequest = {
    request_id: crypto.randomUUID(),
    question,
    context_id: contextId || 'default',
    history_length: 5,
    timeout_seconds: 60,
    metadata: {},
  }
  return unaryCall<AIQueryRequest, AIQueryResponse>(AI_QUERY, 'Query', req)
}

/**
 * Streaming query
 *
 * Uses fetch + ReadableStream to consume server-streaming responses.
 * grpcwebproxy returns gRPC-Web text format (base64-encoded frames),
 * simplified here to line-by-line JSON event parsing.
 */
export function queryStream(
  question: string,
  onEvent: (event: AIStreamEvent) => void,
  contextId?: string,
  signal?: AbortSignal,
): Promise<void> {
  const req: AIQueryRequest = {
    request_id: crypto.randomUUID(),
    question,
    context_id: contextId || 'default',
    history_length: 5,
    timeout_seconds: 120,
    metadata: {},
  }

  const url = `${BASE_URL}${AI_QUERY}/QueryStream`

  const headers: Record<string, string> = { 'Content-Type': 'application/json' }
  const token = _getAuthToken?.()
  if (token) {
    headers['Authorization'] = `Bearer ${token}`
  }

  const controller = new AbortController()
  let timedOut = false
  const timeoutId = setTimeout(() => { timedOut = true; controller.abort() }, 130_000)
  if (signal) {
    signal.addEventListener('abort', () => controller.abort())
  }

  return fetch(url, {
    method: 'POST',
    headers,
    body: JSON.stringify(req),
    signal: controller.signal,
  })
    .then(async (resp) => {
      clearTimeout(timeoutId)
      if (!resp.ok) {
        throw new Error(`QueryStream failed: ${resp.status}`)
      }

      const reader = resp.body?.getReader()
      if (!reader) throw new Error('No response body')

      const decoder = new TextDecoder()
      let buffer = ''

      while (true) {
        const { done, value } = await reader.read()
        if (done) break

        buffer += decoder.decode(value, { stream: true })
        // Split by newline, process SSE-style frames
        const lines = buffer.split('\n')
        buffer = lines.pop() || '' // last line may be incomplete

        for (const line of lines) {
          const trimmed = line.trim()
          if (!trimmed || trimmed.startsWith(':')) continue
          // Try parsing data: prefix (SSE format) or direct JSON
          const jsonStr = trimmed.startsWith('data: ')
            ? trimmed.slice(6)
            : trimmed
          try {
            const event = JSON.parse(jsonStr) as AIStreamEvent
            onEvent(event)
          } catch {
            // non-JSON line, skip
          }
        }
      }

      // Process remaining content in buffer
      if (buffer.trim()) {
        try {
          const event = JSON.parse(buffer.trim()) as AIStreamEvent
          onEvent(event)
        } catch {
          // ignore
        }
      }
    })
    .catch((err) => {
      clearTimeout(timeoutId)
      // Emit error for timeout abort or non-abort errors; skip only user-initiated abort
      if (err.name === 'AbortError' && !timedOut) return
      onEvent({
        event_id: '',
        event_type: 'error',
        content: timedOut ? 'Request timed out, please try again' : (err.message || 'Stream failed'),
        task_state: 'failed',
        context_id: contextId || 'default',
        timestamp: Date.now(),
      })
    })
}

// ============================================================================
// AgentCommunicationService
// ============================================================================

/** Get all registered Agents */
export async function getAgents(
  filter = '',
  limit = 100,
): Promise<GetAgentsResponse> {
  return unaryCall(AGENT_COMM, 'GetAgents', {
    filter,
    limit,
    offset: 0,
  })
}

/** Find Agent by tag/skill/keyword */
export async function findAgents(
  params: Partial<FindAgentsRequest>,
): Promise<FindAgentsResponse> {
  return unaryCall(AGENT_COMM, 'FindAgents', {
    tag: '',
    skill: '',
    keyword: '',
    limit: 100,
    ...params,
  })
}

/**
 * 获取指定Agent的运行时指标
 * 对应 RPC: AIQueryService/GetAgentMetrics
 */
export async function getAgentMetrics(
  agentId: string,
): Promise<{ data: AgentMetrics | null; error?: string }> {
  try {
    const response = await unaryCall<{ agent_id: string }, GetAgentMetricsResponse>(
      AGENT_COMM,
      'GetAgentMetrics',
      { agent_id: agentId },
    )
    return { data: response as AgentMetrics ?? null }
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e)
    console.warn(`Failed to get metrics for ${agentId}:`, e)
    return { data: null, error: msg }
  }
}

// ============================================================================
// UserService (Auth)
// ============================================================================

/** User registration */
export async function register(
  username: string,
  password: string,
  displayName = '',
): Promise<RegisterResponse> {
  const req: RegisterRequest = { username, password, display_name: displayName }
  return unaryCall<RegisterRequest, RegisterResponse>(USER_AUTH, 'Register', req)
}

/** User login */
export async function login(
  username: string,
  password: string,
): Promise<LoginResponse> {
  const req: LoginRequest = { username, password }
  return unaryCall<LoginRequest, LoginResponse>(USER_AUTH, 'Login', req)
}

// ============================================================================
// ObservabilityService
// ============================================================================

/**
 * 获取追踪详情
 */
export async function getTraceDetail(traceId: string): Promise<GetTraceDetailResponse | null> {
  try {
    return await unaryCall<{ trace_id: string }, GetTraceDetailResponse>(
      OBSERVABILITY, 'GetTraceDetail', { trace_id: traceId }
    )
  } catch (e) {
    console.warn('Failed to get trace detail:', e)
    return null
  }
}

/**
 * 获取成本报告
 */
export async function getCostReport(
  userId: string,
  startDate: string,
  endDate: string
): Promise<GetCostReportResponse | null> {
  try {
    return await unaryCall<{ user_id: string; start_date: string; end_date: string }, GetCostReportResponse>(
      OBSERVABILITY, 'GetCostReport', { user_id: userId, start_date: startDate, end_date: endDate }
    )
  } catch (e) {
    console.warn('Failed to get cost report:', e)
    return null
  }
}
